local _M = {}

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

function _M.UploadMedia()
  local upload = require "resty.upload"
  local cjson = require "cjson"
  local GenericObjectPool = require "GenericObjectPool"
  local BackEndServiceClient = require "social_network_BackEndService".BackEndServiceClient
  local ngx = ngx

  local client = GenericObjectPool:connection(
      BackEndServiceClient, "back-end-service", 9091)

  local chunk_size = 8196
  local form, err = upload:new(chunk_size)
  if not form then
    ngx.log(ngx.ERR, "failed to new upload: ", err)
    ngx.exit(500)
  end

  form:set_timeout(1000)
  local media_id = tonumber(string.sub(ngx.var.request_id, 0, 15), 16)
  media_id = string.format("%.f", media_id)
  local media_file = ""
  local media_type

  while true do
    local typ, res, err = form:read()
    if not typ then
      ngx.say("failed to read: ", err)
      return
    end

    if typ == "header" then
      for i, ele in ipairs(res) do
        local filename = string.match(ele, 'filename="(.*)"')
        if filename and filename ~= '' then
          local filename_list = _StringSplit(filename, '.')
          media_type = filename_list[#filename_list]
        end
      end
    elseif typ == "body" then
      media_file = media_file .. res
    elseif typ == "part_end" then

    elseif typ == "eof" then
      break
    end
  end

  local filename = media_id .. '.' ..  media_type
  status, err = pcall(client.UploadMedia, client, filename, media_file)

  if not status then
    ngx.status = ngx.HTTP_INTERNAL_SERVER_ERROR
    ngx.say("Upload Media Failed: " .. err.message)
    ngx.log(ngx.ERR, "Upload Media Failed: " .. err.message)
    ngx.exit(ngx.HTTP_INTERNAL_SERVER_ERROR)
  end

  GenericObjectPool:returnConnection(client)
  ngx.header.content_type = "application/json; charset=utf-8"
  ngx.say(cjson.encode({media_id = media_id, media_type = media_type}))

end

return _M