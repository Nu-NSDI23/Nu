local _M = {}

local function _StrIsEmpty(s)
  return s == nil or s == ''
end

local function _LoadFollwer(data)
  local follower_list = {}
  for _, follower in ipairs(data) do
    local new_follower = {}
    new_follower["follower_id"] = tostring(follower)
    table.insert(follower_list, new_follower)
  end
  return follower_list
end





function _M.GetFollower()
  local ngx = ngx
  local GenericObjectPool = require "GenericObjectPool"
  local BackEndServiceClient = require "social_network_BackEndService".BackEndServiceClient
  local cjson = require "cjson"
  local jwt = require "resty.jwt"
  local liblualongnumber = require "liblualongnumber"

  if (_StrIsEmpty(ngx.var.cookie_login_token)) then
    ngx.status = ngx.HTTP_UNAUTHORIZED
    ngx.exit(ngx.HTTP_OK)
  end

  local login_obj = jwt:verify(ngx.shared.config:get("secret"), ngx.var.cookie_login_token)
  if not login_obj["verified"] then
    ngx.status = ngx.HTTP_UNAUTHORIZED
    ngx.say(login_obj.reason);
    ngx.exit(ngx.HTTP_OK)
  end

  local timestamp = tonumber(login_obj["payload"]["timestamp"])
  local ttl = tonumber(login_obj["payload"]["ttl"])
  local user_id = tonumber(login_obj["payload"]["user_id"])

  if (timestamp + ttl < ngx.time()) then
    ngx.status = ngx.HTTP_UNAUTHORIZED
    ngx.header.content_type = "text/plain"
    ngx.say("Login token expired, please log in again")
    ngx.exit(ngx.HTTP_OK)
  else
    local client = GenericObjectPool:connection(
      BackEndServiceClient, "back-end-service", 9091)
    local status, ret = pcall(client.GetFollowers, client,
        user_id)
    GenericObjectPool:returnConnection(client)
    if not status then
      ngx.status = ngx.HTTP_INTERNAL_SERVER_ERROR
      if (ret.message) then
        ngx.header.content_type = "text/plain"
        ngx.say("Get user-timeline failure: " .. ret.message)
        ngx.log(ngx.ERR, "Get user-timeline failure: " .. ret.message)
      else
        ngx.header.content_type = "text/plain"
        ngx.say("Get user-timeline failure: " .. ret.message)
        ngx.log(ngx.ERR, "Get user-timeline failure: " .. ret.message)
      end
      ngx.exit(ngx.HTTP_INTERNAL_SERVER_ERROR)
    else
      local follower_list = _LoadFollwer(ret)
      ngx.header.content_type = "application/json; charset=utf-8"
      ngx.say(cjson.encode(follower_list) )
    end
  end
end
return _M