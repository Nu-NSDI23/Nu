local _M = {}

local function _StrIsEmpty(s)
  return s == nil or s == ''
end

function _M.Follow()
  local ngx = ngx
  local GenericObjectPool = require "GenericObjectPool"
  local BackEndServiceClient = require "social_network_BackEndService".BackEndServiceClient

  ngx.req.read_body()
  local post = ngx.req.get_post_args()

  local client = GenericObjectPool:connection(
      BackEndServiceClient, "back-end-service", 9091)

  local status
  local err
  if (not _StrIsEmpty(post.user_id) and not _StrIsEmpty(post.followee_id)) then
    status, err = pcall(client.Follow, client,
        tonumber(post.user_id), tonumber(post.followee_id))
  elseif (not _StrIsEmpty(post.user_name) and not _StrIsEmpty(post.followee_name)) then
    status, err = pcall(client.FollowWithUsername, client,
        post.user_name, post.followee_name)
  else
    ngx.status = ngx.HTTP_BAD_REQUEST
    ngx.say("Incomplete arguments")
    ngx.log(ngx.ERR, "Incomplete arguments")
    ngx.exit(ngx.HTTP_BAD_REQUEST)
  end

  if not status then
    ngx.status = ngx.HTTP_INTERNAL_SERVER_ERROR
    if (err.message) then
      ngx.say("Follow Failed: " .. err.message)
      ngx.log(ngx.ERR, "Follow Failed: " .. err.message)
    else
      ngx.say("Follow Failed: " .. err)
      ngx.log(ngx.ERR, "Follow Failed: " .. err)
    end
    client.iprot.trans:close()
    ngx.exit(ngx.HTTP_INTERNAL_SERVER_ERROR)
  else
    ngx.say("Success!")
    GenericObjectPool:returnConnection(client)
  end

end

return _M