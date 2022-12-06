local fs = require("fs")
local path = require("path")

local layers = 6
local max_open_handles = 500

local function getAmt(layer)
	if layer <= 0 then return 1 end
	return layer + 1
end

local function getName(num, typ)
	return ("%s_%d%s"):format(typ, num, typ == "file" and ".dat" or "")
end

local function resume(...)
	local ok, err = coroutine.resume(...)
	if not ok then error(err) end
end

local open_handles = 0
local coro
local total = 0
local yielded = {}

local function incrHandles()
	if total % 250 == 0 then print(total) end

	open_handles = open_handles + 2

	if open_handles > max_open_handles then
		yielded[coroutine.running()] = true
		coroutine.yield()
	end
end

local function decrHandles(me)
	open_handles = open_handles - 1

	if open_handles < max_open_handles then
		-- we're blocked, unblock us
		if coroutine.status(me) == "suspended" then
			yielded[me] = nil
			resume(me)
		else -- unblock the next poor guy
			for k in pairs(yielded) do
				yielded[k] = nil
				resume(k)
				break
			end
		end
	end
end

local function make(layer, curpath)
	if layer > layers then return end

	local fls = getAmt(layer)
	local me = coroutine.running()

	for i=1, fls do
		local folderpath = path.join(curpath, getName(i, "folder"))
		local filepath = path.join(curpath, getName(i, "file"))

		total = total + 2

		fs.mkdir(folderpath, function(err)
			if err and not err:match("^EEXIST") then error(err) end
			decrHandles(me)
			resume(coroutine.create(make), layer + 1, folderpath)
		end)

		fs.writeFile(filepath, ("Layer %d, path: %s"):format(layer, filepath), function(err)
			if err and not err:match("^EEXIST") then error(err) end
			decrHandles(me)
		end)

		incrHandles()
	end
end

fs.mkdirSync("test/")
coro = coroutine.create(make)

resume(coro, 1, "test/")