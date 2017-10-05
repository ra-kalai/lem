local io = require 'lem.io'

local t = io.spawnp({"/bin/sh", "-c", "read var1 ; echo $var1; env; sleep 1"}
                ,{
                    --{fds={1, 2}, kind='pipe', name='stdstream'},
                    --{fds={0}, kind='pipe', name='stdin', mode="w"}
                    {fds={0,1,2}, kind='pty', name='stdstream'}
                }
                ,{
                  TOTO='VAL1',
                  TITIT='VAL2'
                }
                ,{
                  LEM_SPAWN_SETSID=1,
                  LEM_SPAWN_SCTTY=1
                }
               )


t.stream.stdstream:write("ooo\n")
while true do
	local l, err = t.stream.stdstream:read("*l")
	if err then
		return
	end
	print(l)
end
