local _M = {}

local function _StrIsEmpty(s)
  return s == nil or s == ''
end

local function _StringSplit(input_str, sep)
  if sep == nil then
    sep = "%s"
  end
  local t = {}
  for str in string.gmatch(input_str, "([^"..sep.."]+)") do
    table.insert(t, str)
  end
  return t
end

function _M.GetMedia()
  local ngx = ngx
  local GenericObjectPool = require "GenericObjectPool"
  local BackEndServiceClient = require "social_network_BackEndService".BackEndServiceClient

  local client = GenericObjectPool:connection(
      BackEndServiceClient, "back-end-service", 9091)

  local chunk_size = 255 * 1024

  ngx.req.read_body()
  local args = ngx.req.get_uri_args()
  if (_StrIsEmpty(args.filename)) then
    ngx.status = ngx.HTTP_BAD_REQUEST
    ngx.say("Incomplete arguments")
    ngx.log(ngx.ERR, "Incomplete arguments")
    ngx.exit(ngx.HTTP_BAD_REQUEST)
  end

  status, ret = pcall(client.GetMedia, client, args.filename)

  if not status then
    ngx.status = ngx.HTTP_INTERNAL_SERVER_ERROR
    ngx.say("Get Media Failed: " .. ret.message)
    ngx.log(ngx.ERR, "Get Media Failed: " .. ret.message)
    ngx.exit(ngx.HTTP_INTERNAL_SERVER_ERROR)
  end

  local media_file = ret

  local filename_list = _StringSplit(args.filename, '.')
  local media_type = filename_list[#filename_list]

  ngx.header.content_type = "image/" .. media_type
  ngx.say(media_file)

end

return _M