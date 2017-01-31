#!bin/lem
--
-- This file is part of LEM, a Lua Event Machine.
-- Copyright 2013 Emil Renner Berthing
--
-- LEM is free software: you can redistribute it and/or modify it
-- under the terms of the GNU Lesser General Public License as
-- published by the Free Software Foundation, either version 3 of
-- the License, or (at your option) any later version.
--
-- LEM is distributed in the hope that it will be useful, but
-- WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
-- GNU Lesser General Public License for more details.
--
-- You should have received a copy of the GNU Lesser General Public
-- License along with LEM.  If not, see <http://www.gnu.org/licenses/>.
--

package.path = '?.lua'
package.cpath = '?.so;?.dll'

local utils = require 'lem.utils'
local io    = require 'lem.io'

local spawn = utils.spawn

local s1, s2 = io.unix.socketpair()

print(s1:fileno(), s2:fileno())

spawn(function ()
	local n = 0
	while n < 10 do
		local l = s2:read("*l")
		s2:write("ok\n")
		print('s2> '.. l)
		n = n + 1
	end
end)

for i=0, 9 do
	print('s1 write', i)
	s1:write(i.."\n")
	print('s2 writeback', s1:read("*l"))
end

-- vim: syntax=lua ts=2 sw=2 noet:
