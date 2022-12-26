local uv = require("uv")
local childprocess = require("childprocess")
local timer = require("timer")

local checks = 250
local simul = 5

coroutine.wrap(function()
	local coro = coroutine.running()

	for go = 1, checks do
		for sim = 1, simul do
			local proc = childprocess.spawn("./sleepsort.out")

			local fullStr = ""

			proc.stdout:on("data", function(str)
				fullStr = fullStr .. str
			end)

			local strings = {}
			local lengths = {}

			for i=1, 100 do
				local len = math.random(1, 60)
				lengths[len] = (lengths[len] or 0) + 1
				strings[#strings + 1] = string.char(math.random(97, 122)):rep(len)
			end

			for k,v in pairs(strings) do
				proc.stdin:write(v .. "\n")
			end

			proc.stdin:write("\n")

			proc.stdout:on("close", function()
				local lastLen = 0
				local lastStr = ""

				for s in fullStr:gmatch("[^\r\n]+") do
					if #s < lastLen then
						print("epic fail", lastLen, lastStr, #s, s)
						os.exit()
						return
					end

					lengths[#s] = lengths[#s] - 1
					lastLen = #s
					lastStr = s
				end

				for k,v in pairs(lengths) do
					if v ~= 0 then
						print("missing string of length", k)
						os.exit()
						return
					end
				end
				coroutine.resume(coro)
			end)
		end

		for i=1, simul do
			coroutine.yield()
		end
		print("check #" .. go .. " complete, all good (" .. simul .. " simultaneous processes)")
	end

	os.exit()
end)()