local _M = {}

local function _StrIsEmpty(s)
  return s == nil or s == ''
end

function _M.ComposePost()

  local ngx = ngx
  local cjson = require "cjson"
  local jwt = require "resty.jwt"

  local GenericObjectPool = require "GenericObjectPool"
  local BackEndServiceClient = require "social_network_BackEndService".BackEndServiceClient

  GenericObjectPool:setMaxTotal(512)

  ngx.req.read_body()
  local post = ngx.req.get_post_args()

  if (_StrIsEmpty(post.post_type) or _StrIsEmpty(post.text)) then
    ngx.status = ngx.HTTP_BAD_REQUEST
    ngx.say("Incomplete arguments")
    ngx.log(ngx.ERR, "Incomplete arguments")
    ngx.exit(ngx.HTTP_BAD_REQUEST)
  end

  if (_StrIsEmpty(ngx.var.cookie_login_token)) then
    ngx.status = ngx.HTTP_UNAUTHORIZED
    ngx.redirect("../../index.html")
    ngx.exit(ngx.HTTP_OK)
  end

  local login_obj = jwt:verify(ngx.shared.config:get("secret"), ngx.var.cookie_login_token)
  if not login_obj["verified"] then
    ngx.status = ngx.HTTP_UNAUTHORIZED
    ngx.say(login_obj.reason);
    ngx.redirect("../../index.html")
    ngx.exit(ngx.HTTP_OK)
  end
  -- get user id/name from login obj
  local timestamp = tonumber(login_obj["payload"]["timestamp"])
  local ttl = tonumber(login_obj["payload"]["ttl"])
  local user_id = tonumber(login_obj["payload"]["user_id"])
  local username = login_obj["payload"]["username"]

  if (timestamp + ttl < ngx.time()) then
    ngx.status = ngx.HTTP_UNAUTHORIZED
    ngx.header.content_type = "text/plain"
    ngx.say("Login token expired, please log in again")
    ngx.redirect("../../index.html")
    ngx.exit(ngx.HTTP_OK)
  else
    local status, ret
    local client = GenericObjectPool:connection(
      BackEndServiceClient, "back-end-service", 9091)

    if (not _StrIsEmpty(post.media_ids) and not _StrIsEmpty(post.media_types)) then
      status, ret = pcall(client.ComposePost, client,
          username, tonumber(user_id), post.text,
          cjson.decode(post.media_ids), cjson.decode(post.media_types),
          tonumber(post.post_type))
    else
      status, ret = pcall(client.ComposePost, client,
          username, tonumber(user_id), post.text,
          {}, {}, tonumber(post.post_type))
    end

    if not status then
      ngx.status = ngx.HTTP_INTERNAL_SERVER_ERROR
      if (ret.message) then
        ngx.say("compost_post failure: " .. ret.message)
        ngx.log(ngx.ERR, "compost_post failure: " .. ret.message)
      else
        ngx.say("compost_post failure: " .. ret)
        ngx.log(ngx.ERR, "compost_post failure: " .. ret)
      end
      client.iprot.trans:close()
      ngx.exit(ngx.status)
    end
  
    GenericObjectPool:returnConnection(client)
    ngx.status = ngx.HTTP_OK
    ngx.say("Successfully upload post")
    ngx.exit(ngx.status)
  end
end

return _M