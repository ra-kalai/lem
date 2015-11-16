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

local lfs = require 'lem.lfs'
local io = require 'lem.io'
local glob = lfs.glob


local serialize
local loadbc
if _VERSION == 'Lua 5.1' then
  serialize = string.dump
  lstring = 'loadstring'
  loadbc = loadstring
else
  serialize = function (f) return string.dump(f, true) end
  --serialize = function (f) return string.dump(f) end
  lstring = 'load'
  loadbc = load
end

local lualib ={}
lualib[#lualib+1] = [==[
(function ()
  local lstring = ]==] .. lstring .. [==[

  local g_require = {
]==]
local c

for i, path in pairs({'lem/*/*.lua','lem/*.lua'}) do
  for i, v in pairs(glob(path)) do
    c = io.open(v):read("*a")
    lualib[#lualib+1] = string.format("[%q]=lstring(%q),", v:gsub('.lua$','')
                                                            :gsub('/','.'), serialize(loadbc(c)))
  end
end

lualib[#lualib+1] = [==[
  }

  local old_require = require
  require = function (modname)
    local f = g_require[modname]
    if f then
      return f()
    end

    return old_require(modname)
  end
end)()
]==]


local c = 0
local out = string.format([[
#include <stdint.h>
uint8_t lem_lualib_preamble[] = {
%s]],
                        serialize(loadbc(table.concat(lualib, '')))
                        :gsub(".", function (m)
                          c = c + 1
                          if c % 20 == 0 then
                            return '0x' .. string.format('%02x,\n', string.byte(m))
                          else
                            return '0x' .. string.format('%02x,', string.byte(m))
                          end
                        end) ..
                        '};')


print(out)

-- vim: syntax=lua ts=2 sw=2 noet:
