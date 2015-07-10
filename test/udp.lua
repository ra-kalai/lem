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
package.cpath = '?.so'

local io    = require 'lem.io'
local write, format = io.write, string.format

local port = arg[2] or 3128

if arg[1] == 'client' then
	local payload_list = {"hello", "bla"}
	sock = io.udp.connect('127.0.0.1', 3128)
	for i, payload in pairs(payload_list) do
		sock:write(payload)
	end
else
	sock = io.udp.listen4("*", 3128)
	sock:autospawn(function (datagram, ip, port)
		print(datagram, ip, port)
	end)
end

-- vim: syntax=lua ts=2 sw=2 noet:
