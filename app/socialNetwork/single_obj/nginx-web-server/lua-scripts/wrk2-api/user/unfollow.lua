local _M = {}

local function _StrIsEmpty(s)
  return s == nil or s == ''
end

function _M.Unfollow()
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
    status, err = pcall(client.Unfollow, client,
        tonumber(post.user_id), tonumber(post.followee_id))
  elseif (not _StrIsEmpty(post.user_name) and not _StrIsEmpty(post.followee_name)) then
    status, err = pcall(client.UnfollowWithUsername, client,
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
      ngx.say("Unfollow Failed: " .. err.message)
      ngx.log(ngx.ERR, "Unfollow Failed: " .. err.message)
    else
      ngx.say("Unfollow Failed: " .. err)
      ngx.log(ngx.ERR, "Unfollow Failed: " .. err)
    end
    client.iprot.trans:close()
    ngx.exit(ngx.HTTP_INTERNAL_SERVER_ERROR)
  end

  ngx.say("Success!")
  GenericObjectPool:returnConnection(client)

end

return _M