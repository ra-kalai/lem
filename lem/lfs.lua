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

local function glob(path, mode, cpath, lvl)
	mode = mode or 'f'

	if type(path) == 'string' then
		local path_t = {}
		if path:match("^/") then
			path_t[-1] = '/'
		else
			path_t[-1] = './'
		end

		for w in path:gmatch("([^/]+)/?") do
			path_t[#path_t+1] = w:gsub('%.', '%%.'):gsub("%*", ".*")..'$'
		end
		return glob(path_t, mode, path_t[-1], 1)
	end

	local list_file, list_dir, ret_stats

	if mode:match('d') then list_dir = true end
	if mode:match('f') then list_file = true end
	if mode:match('s') then ret_stats = true end

	local dir_iter, k = lfs.dir(cpath)
	local ret = {}
	local name

	if dir_iter == nil then
		return ret
	end

	repeat
		name = dir_iter(k)

		if name and
			 name ~= '.' and
			 name ~= '..' then

			if path[lvl] == nil then
				return ret
			end

			if name:match(path[lvl]) then
				local attr = lfs.attributes(cpath .. name)
				if attr then
					if attr.mode == 'file' then
						if list_file and lvl == #path then
							if ret_stats then
								ret[#ret + 1] = {cpath .. name, attr}
							else
								ret[#ret + 1] = cpath .. name
							end
						end
					elseif attr.mode == 'directory' then
						local subret = glob(path, mode, cpath .. name .. '/', lvl + 1)
						for i=1, #subret do
							ret[#ret+1] = subret[i]
						end
						if lvl == #path and list_dir then
							if ret_stats then
								ret[#ret + 1] = {cpath .. name, attr}
							else
								ret[#ret + 1] = cpath .. name
							end
						end
					end
				end
			end
		end
	until name == nil

	return ret
end

lfs.glob = glob

return lfs

-- vim: ts=2 sw=2 noet:
