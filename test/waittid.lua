#!bin/lem
--
-- This file is part of LEM, a Lua Event Machine.
-- Copyright 2011-2012 Emil Renner Berthing
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

local utils = require 'lem.utils'

local spawn2 = utils.spawn2
local waittid = utils.waittid

function sleep(t)
	local sleeper = utils.newsleeper()
	sleeper:sleep(t)
end

print('starting some thread')

local towait4 = {}

for i=0,10 do
	towait4[#towait4+1] =
		spawn2(function (a1)
			print('..', a1)
			sleep(i/1000.)
			print(a1, '..')
		end, i)
end

utils.yield()

print('waiting for',#towait4, 'thread to finish')
local join_count = waittid(towait4)
print('joined', join_count, ' thread of', #towait4, ";", #towait4 - join_count, ' thread was/were already dead')


-- vim: syntax=lua ts=2 sw=2 noet:
