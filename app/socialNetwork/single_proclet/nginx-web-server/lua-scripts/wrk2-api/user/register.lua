local _M = {}

local function _StrIsEmpty(s)
  return s == nil or s == ''
end

function _M.RegisterUser()
  local ngx = ngx
  local GenericObjectPool = require "GenericObjectPool"
  local BackEndServiceClient = require "social_network_BackEndService".BackEndServiceClient

  ngx.req.read_body()
  local post = ngx.req.get_post_args()

  if (_StrIsEmpty(post.first_name) or _StrIsEmpty(post.last_name) or
      _StrIsEmpty(post.username) or _StrIsEmpty(post.password) or
      _StrIsEmpty(post.user_id)) then
    ngx.status = ngx.HTTP_BAD_REQUEST
    ngx.say("Incomplete arguments")
    ngx.log(ngx.ERR, "Incomplete arguments")
    ngx.exit(ngx.HTTP_BAD_REQUEST)
  end

  local client = GenericObjectPool:connection(BackEndServiceClient, "back-end-service", 9091)

  local status, err = pcall(client.RegisterUserWithId, client, post.first_name,
      post.last_name, post.username, post.password, tonumber(post.user_id))

  if not status then
    ngx.status = ngx.HTTP_INTERNAL_SERVER_ERROR
    if (err.message) then
      ngx.say("User registration failure: " .. err.message)
      ngx.log(ngx.ERR, "User registration failure: " .. err.message)
    else
      ngx.say("User registration failure: " .. err)
      ngx.log(ngx.ERR, "User registration failure: " .. err)
    end
    client.iprot.trans:close()
    ngx.exit(ngx.HTTP_INTERNAL_SERVER_ERROR)
  end

  ngx.say("Success!")
  GenericObjectPool:returnConnection(client)
end

return _M