-- examples/app.lua - 脚本打包示例：会被嵌进单文件 exe
--
-- 打包：
--   scripts\build-lua.cmd -Script examples\app.lua -Bytecode -With cjson,miniz
--   bin\app.exe self

local cmd = arg[1] or 'help'

local function human(n)
  n = tonumber(n) or 0
  local units = { {'TiB', 1099511627776}, {'GiB', 1073741824},
                  {'MiB', 1048576}, {'KiB', 1024} }
  for _, u in ipairs(units) do
    if n >= u[2] then return string.format('%.2f %s', n / u[2], u[1]) end
  end
  return n .. ' B'
end

if cmd == 'help' then
  print('便携 Lua 工具（脚本已编译进 exe）')
  print('  app self                 自检：确认内嵌库可用')
  print('  app size <字节数>         人类可读容量')
  print('  app json <JSON 字符串>    解析并格式化输出')
  print('  app zip  <字符串>         压缩并显示压缩率')
elseif cmd == 'self' then
  print('Lua 版本    : ' .. _VERSION)
  print('参数表      : arg[1]=' .. tostring(arg[1]))
  print('整数语义    : 7//2 = ' .. (7 // 2) .. ', math.type(1) = ' .. math.type(1))
  print('位运算      : 0xF0 | 0x0F = ' .. (0xF0 | 0x0F))
  print('utf8 长度   : ' .. utf8.len('héllo→世界') .. ' 码点')
  if C.json_decode then
    local ok = C.json_decode('{"a":1}')
    print('cJSON 绑定  : ' .. (ok and ok.a == 1 and '可用' or '异常'))
  end
  if C.gzip_compress then
    local src = string.rep('TCCWIN', 500)
    local z = C.gzip_compress(src)
    print('miniz 绑定  : 压缩 ' .. #src .. ' -> ' .. #z .. ' 字节')
    print('CRC32       : ' .. string.format('%08X', C.crc32('hello')))
  end
elseif cmd == 'size' then
  print(arg[2] .. ' = ' .. human(arg[2]))
elseif cmd == 'json' then
  local v, err = C.json_decode(arg[2])
  if not v then print('解析失败: ' .. tostring(err)) os.exit(1) end
  local function dump(t, ind)
    ind = ind or ''
    local keys = {}
    for k in pairs(t) do keys[#keys + 1] = k end
    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
    for _, k in ipairs(keys) do
      local val = t[k]
      if type(val) == 'table' then
        print(ind .. tostring(k) .. ':')
        dump(val, ind .. '  ')
      else
        print(string.format('%s%-16s %s', ind, tostring(k), tostring(val)))
      end
    end
  end
  dump(v)
elseif cmd == 'zip' then
  local s = arg[2] or 'hello world'
  local z = C.gzip_compress(s)
  print(string.format('%d 字节 -> %d 字节 (%.1f%%)', #s, #z, 100 * #z / #s))
  local hex = (z:gsub('.', function(c) return string.format('%02X', c:byte()) end))
  print('zlib 流: ' .. hex)
  print('解压回原: ' .. tostring(C.gzip_decompress(z) == s))
else
  print('未知命令: ' .. tostring(cmd))
  os.exit(2)
end
