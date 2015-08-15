--
-- This file is part of LEM, a Lua Event Machine.
-- Copyright 2012 Emil Renner Berthing
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

local lfs = require 'lem.lfs.core'

function lfs.lock(file, ...)
	return file:lock(...)
end

function lfs.unlock(file, ...)
	return file:lock('u', ...)
end

function lfs.setmode()
	return 'binary'
end

do
	local setmetatable, remove, link = setmetatable, lfs.remove, lfs.link

	local Lock = { __index = true, free = true }
	Lock.__index = Lock

	function Lock:free()
		return remove(self.filename)
	end

	function lfs.lock_dir(path)
		local filename = path .. '/lockfile.lfs'
		local ok, err = link('lock', filename, true)
		if not ok then return ok, err end

		return setmetatable({ filename = filename }, Lock)
	end
end

function lfs.glob(path, kind)
	kind = kind or 'df'
	local folder =  {}
	local ret = {}

	local list_file
	local list_dir

	if kind:match('d') then list_dir = true end
	if kind:match('f') then list_file = true end

	path:gsub('(/?[^/]+/?)', function (match)
		folder[#folder+1] = match
	end)

	local dirpath = ''
	local leave_first_loop = false

	for i, cdir in pairs(folder) do

		if cdir:match('%*') then
			for name in lfs.dir(dirpath) do
				if name ~= '.'  and
					 name ~= '..' then

					if name:match(cdir:gsub("%*",".*"):gsub('/','')) then
						local attr = lfs.attributes(dirpath .. name)

						if attr.mode == 'file' then
							if list_file and i == #folder then
								ret[#ret+1] = dirpath .. name
							end
						else
							if list_dir and i == #folder then
								ret[#ret+1] = dirpath .. name
							else
								local path_down = dirpath .. name .. '/'

								for i2 = i+1, #folder do
									path_down = path_down .. folder[i2]
								end
								local lret = lfs.glob(path_down, kind)
								for li=1,#lret do
									ret[#ret+1] = lret[li]
								end
							end
						end

						leave_first_loop = true
					end
				end
			end
		else
			dirpath = dirpath .. cdir
		end

		if leave_first_loop == true then break end
	end

	return ret
end

return lfs

-- vim: ts=2 sw=2 noet:
