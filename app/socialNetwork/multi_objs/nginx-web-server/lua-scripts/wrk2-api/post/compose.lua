local _M = {}

local function _StrIsEmpty(s)
  return s == nil or s == ''
end

function _M.ComposePost()
  local ngx = ngx
  local cjson = require "cjson"

  local GenericObjectPool = require "GenericObjectPool"
  local BackEndServiceClient = require "social_network_BackEndService".BackEndServiceClient

  GenericObjectPool:setMaxTotal(512)

  ngx.req.read_body()
  local post = ngx.req.get_post_args()

  if (_StrIsEmpty(post.user_id) or _StrIsEmpty(post.username) or
      _StrIsEmpty(post.post_type) or _StrIsEmpty(post.text)) then
    ngx.status = ngx.HTTP_BAD_REQUEST
    ngx.say("Incomplete arguments")
    ngx.log(ngx.ERR, "Incomplete arguments")
    ngx.exit(ngx.HTTP_BAD_REQUEST)
  end

  local status, ret

  local client = GenericObjectPool:connection(
      BackEndServiceClient, "back-end-service", 9091)

  if (not _StrIsEmpty(post.media_ids) and not _StrIsEmpty(post.media_types)) then
    status, ret = pcall(client.ComposePost, client,
        post.username, tonumber(post.user_id), post.text,
        cjson.decode(post.media_ids), cjson.decode(post.media_types),
        tonumber(post.post_type))
  else
    status, ret = pcall(client.ComposePost, client,
        post.username, tonumber(post.user_id), post.text,
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

return _M