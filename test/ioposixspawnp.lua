local io = require 'lem.io'

local t = io.posix_spawnp({"/bin/sh", "-c", "read var1 ; echo $var1; env; sleep 1"}
                ,{
                    {fds={1, 2}, kind='pipe', name='stdstream'},
                    {fds={0}, kind='pipe', name='stdin', mode="w"}
                }
                ,{
                  TOTO='VAL1',
                  TITIT='VAL2'
                }
               )


t.stream.stdin:write("ooo\n")
print(t.stream.stdstream:read("*a"))
