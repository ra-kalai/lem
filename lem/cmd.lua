--
-- This file is part of LEM, a Lua Event Machine.
-- Copyright 2011-2013 Emil Renner Berthing
-- Copyright 2012-2013 Asbjørn Sloth Tønnesen
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

local utils = require 'lem.utils'
local io = require 'lem.io'
local format = string.format

local M = {}

local args_meta = {}

args_meta.is_flag_set = function (self, arg)
	if self[arg] then
		return (self[arg] and self[arg] > 0)
	else
		return nil
	end
end

args_meta.get_last_val = function (self, arg)
	if self[arg] then
		return self[arg][#self[arg]]
	else
		return nil
	end
end


M.parse_arg = function (args, argv, start)
	start = start or 1
	argv = argv or {}
	local ret = {self_exe = argv[0], last_arg = {}}
	setmetatable(ret, {__index=args_meta})
	local err

	local pmap = {}
	for i, v in pairs(args.possible) do
		pmap[v[1]] = v
		pmap[v[2]] = v
	end

	local i = start
	local last_arg_idx = 0

	while i <= #argv do
		local carg = argv[i]
		local argtry = carg

		if argtry ~= '-' and argtry ~= '--' and #argtry>1 then
			argtry = carg:sub(2)
		end

		if ret['--'] == nil and ret.last_arg[0] == nil and pmap[argtry] ~= nil then
			local key = pmap[argtry][2]

			if (pmap[argtry][3].type == 'counter') then
				ret[key] = ret[key] or 0
				ret[key] = ret[key] + 1
			else
				ret[key] = ret[key] or {}
				if i+1 <= #argv then
					ret[key][#ret[key]+1] = argv[i+1]
				else
					err = format("missing '%s' payload", carg)
					break
				end
				i = i + 1
			end
		elseif ret.last_arg[0] == nil and (carg == '-' or carg == '--') then
			ret[carg] = ret[carg] or 0
			ret[carg] = ret[carg] + 1
		else
			if ret.last_arg[0] == nil then
				if ret['--'] == nil and carg:match("^-") then
					err = format("unrecognized option: '%s'", carg)
					break
				end
			end

			ret.last_arg[last_arg_idx] = carg
			last_arg_idx = last_arg_idx + 1
		end

		i = i + 1
	end

	if err == nil then
		for i, v in pairs(args.possible) do
			if v[3].default_value ~= nil and ret[v[2]] == nil then
				ret[v[2]] = {v[3].default_value}
			end
		end
	end

	ret.err = err

	return ret
end

local function stderr_print(...)
	io.stderr:write(format(...))
end

M.display_usage= function (args, err)
	if err.err then
		stderr_print(err.err .."\n")
	end

	local max_arg_len = 0
	for i, v in pairs(args.possible) do
		if max_arg_len < #v[2] then
			max_arg_len = #v[2]
		end
	end

	local max_default_arg_len = 0
	for i, v in pairs(args.possible) do
		local dval = v[3].default_value
		if dval then
			local dval_len = tostring(dval)
			if max_default_arg_len < #dval_len then
				max_default_arg_len = #dval_len
			end
		end
	end

	args.last_arg = args.last_arg or ""
	if args.last_arg ~= "" then
		args.last_arg = " " .. args.last_arg
	end
	stderr_print("usage: %s%s\n", err.self_exe, args.last_arg)
	if args.intro then
		stderr_print(args.intro .. '\n')
	end

	for i, v in pairs(args.possible) do
		if v[2] == v[1] then v[2] = '' end

		if v[1]:sub(1,1) ~= "-" then
			v[1] = '-' .. v[1]
		else
			max_arg_len = max_arg_len + 1
		end

		if max_default_arg_len == 0 then
			stderr_print("  %s %-" .. (max_arg_len+1) .. "s %s\n", v[1], v[2], v[3].desc or "")
		else
			local dval = v[3].default_value

			if dval == nil then
				stderr_print("  %s %-" .. (max_arg_len+1) .. "s %s %s\n", v[1], v[2], string.rep(" ", max_default_arg_len+2), v[3].desc or "")
			else
				stderr_print("  %s %-" .. (max_arg_len+1) .. "s %s %s\n", v[1], v[2],
																																  format("%-" .. (max_default_arg_len+2) .. "s", '[' .. dval .. ']'),
																																	v[3].desc or "")
			end
		end
	end
  
	os.exit(1)
end

if command_chain then
	M.lem_main = function ()
		local arg2 = {}
		arg2[-1] = arg[-1]
		for i = 0, #arg do
			arg2[i+1] = arg[i]
		end
		arg2[0] = command_chain[1]
		arg = arg2
		utils.spawn(command_chain[2])
		command_chain = nil
	end
	return M
end

M.lem_main = function ()
	M.lem_main = nil

	local LEM_VERSION = format("A Lua Event Machine 0.4 (%s) Copyright (C) 2011-2015 Emil Renner Berthing\n", _VERSION)

	local function start_repl() 
		local r = require 'lem.repl'
		local _, err = r.repl('stdin', io.stdin, io.stdout)
		print(err or '')
	end

	if arg[0] == nil then
		-- if no agument is pass it is equivalent to -i
		io.stdout:write(LEM_VERSION)
		start_repl()
	else 
		-- parse argument
		local args = {
			min_arg = 0,
			strict = false,
			last_arg = "[script [args]...]",
			intro = "Available options are:",
			possible = {
				{'h', 'help', {desc="Display this", type='counter'}},
				{'e', 'stat', {desc="Execute string 'stat'"}},
				{'i', 'interactive', {desc="Enter interactive mode after executing 'script'", type='counter'}},
				{'v', 'version', {desc="Show version information", type='counter'}},
				{'b', 'bytecode', {desc="Output bytecode to stdout"}},
				{'-', '-', {desc="Execute stdin", type='counter'}},
			},
		}

		local parg = M.parse_arg(args, arg, 0)

		local load = load
		if _VERSION == 'Lua 5.1' then
			load = loadstring
		end

		if parg.err then
			parg.self_exe = arg[-1]
			M.display_usage(args, parg)
		end

		if parg:is_flag_set('help') then
			M.display_usage(args, {self_exe=arg[-1]})
		end

		if parg:is_flag_set('interactive') then
			io.stdout:write(LEM_VERSION)
		end


		if (parg.stat) then
			for i, v in pairs(parg.stat) do
				load(v)()
			end
		end

		if parg:is_flag_set('-') then
			pcall(load(io.stdin:read("*a")))
		end

		if parg:get_last_val('bytecode') then
			for i, script_to_convert in pairs(parg.bytecode) do
				local file = io.open(script_to_convert)
				assert(file, "could not open file " .. script_to_convert)
				file = file:read("*a")
				script_load, err = load(file)
				assert(err == nil, 'could not load file ' .. script_to_convert)
				print(string.dump(script_load))

				os.exit(0);
			end
		end

		if parg.last_arg and parg.last_arg[0] then
			local script_to_run_filepath = parg.last_arg[0]
			local script_to_run, err = loadfile(script_to_run_filepath)

			if err ~= nil then
				stderr_print('could not loadfile %s\n%s\n', script_to_run_filepath, err)
				os.exit(1)
			end

			parg.last_arg[-1] = arg[-1]
			arg = parg.last_arg
			script_to_run()
		end

		if parg:is_flag_set('interactive') then
			start_repl()
		end

		if parg:is_flag_set('version') then
			io.stdout:write(LEM_VERSION)
			os.exit(0)
		end
	end
end

return M

-- vim: ts=2 sw=2 noet:
