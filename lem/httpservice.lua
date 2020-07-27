local httpserv = require 'lem.http.server'
local httpresp = require 'lem.http.response'
local json = require 'lem.json'

local urldecode = httpserv.urldecode
local parseform = httpserv.parseform

function httpserv.Request:formData()
	return parseform(self:body())
end

do
local json_decode
function httpserv.Request:jsonData()
	local body = self:body()
	local ok, payload = pcall(json.decode, body)

	if ok then
		return payload
	end

	return nil
end
end

local format = string.format

local _HttpServiceMt = {
	mimemap = {
		jpeg = 'image/jpeg',
		jpg = 'image/jpeg',
		png = 'image/png',
		js = 'application/javascript',
		json = 'application/json',
		bin = 'application/octet-stream',
		html = 'text/html',
	}
}

_HttpServiceMt.__index = _HttpServiceMt

function _HttpServiceMt.BindMethod(bind)
	return bind
end

function _HttpServiceMt:debug(kind, msg)
	print(kind, msg)
end

function _HttpServiceMt._defaultDeliverHeader(pl, res)
	if res.headers['Server'] == nil then
		res.headers['Server'] = 'Httpservice/0.0 LEM'
	end
end

function _HttpServiceMt._defaultDeliverCb(pl, res)
	_HttpServiceMt._defaultDeliverHeader(pl, res)
	_HttpServiceMt.deliverHTMLPage(pl, res)
end

function _HttpServiceMt.deliverFile(pl, res)
	_HttpServiceMt._defaultDeliverHeader(pl, res)
	local ext = pl:match("%.[^.]*$")
	if ext then
		ext = ext:sub(2):lower()
	end
	local mime = _HttpServiceMt.mimemap[ext]
	if mime == nil then
		mime = _HttpServiceMt.mimemap.bin
	end
	res.headers['Content-Type'] = mime
	res.file = pl
end

function _HttpServiceMt.deliverHTMLPage(pl, res)
	_HttpServiceMt._defaultDeliverHeader(pl, res)
	if type(pl) == 'table' then
		res:add(table.concat(pl))
	else
		res:add(pl)
	end
	res.headers['Content-Type'] = _HttpServiceMt.mimemap['html']
end

function _HttpServiceMt.deliverJSON(pl, res)
	_HttpServiceMt._defaultDeliverHeader(pl, res)

	local ok, data = pcall(json.encode, pl)
	if ok then
		res:add(data)
	end

	res.headers['Content-Type'] = _HttpServiceMt.mimemap['json']
end

function _HttpServiceMt:handler(req, res)
	self:debug('info', format("%s %s HTTP/%s", req.method, req.uri, req.version))

	if req.method == 'HEAD' then
		req.method = 'GET'
		req.original_method = 'HEAD'
	end

	local route, args = self:_parseReq(req)

	if route then
		local response = route.cb(req, res, args)
		local deliveryCb = route.dcb or self._defaultDeliverCb
		deliveryCb(response, res)
		if req.original_method then
			req.method = req.original_method
		end
	else
			httpresp.method_not_allowed(req, res)
	end
end

function _HttpServiceMt:_parseReq(req)
	local method, path = req.method, req.path

	local methodRoute = self[method]
	if methodRoute then
		local route
		for i=1, #methodRoute do
			route = methodRoute[i]
			local matches = { path:match(route.pattern) }
			if #matches > 0 then
				return route, matches
			end
		end
	end
end

function _HttpServiceMt:serve(conf)
	local err
	self.server, err = httpserv.new(conf.host, conf.port,
		function (req, res) self:handler(req, res) end)

	self.server:run()
end

function _HttpServiceMt.__call(self, init_chains)
	local ret = _HttpService()

	for _, chains in ipairs(init_chains) do
		local method_name = chains[1]
		ret[method_name] = ret[method_name] or {}

		for i=2, #chains do
			local route_info = chains[i]
			route_info.pattern = "^/" .. route_info.match:gsub("%%[_a-z0-9]*", "([^/]*)") .. "$"
			ret[method_name][#ret[method_name]+1] = route_info
		end
	end

	return ret
end

function _HttpService()
	return setmetatable({}, _HttpServiceMt)
end

local HttpService = _HttpService()

return HttpService
