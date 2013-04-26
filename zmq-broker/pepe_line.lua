local hostlist = require ("hostlist")

if pepe.rank == 0 then
    local env = pepe:getenv()
    local v,err = pepe:getenv("ENV")
    if not v then
	print ("getenv(ENV): " .. err .. "\n")
    end
    pepe:unsetenv ("ENV")
    pepe:setenv ("HAVE_PEPE", 1)
    pepe:setenv ("PS1", "${SLURM_JOB_NODELIST} \\\u@\\\h \\\w$ ")

end

local h = hostlist.new (pepe.nodelist)
local eventuri = "epgm://eth0;239.192.1.1:5555"
local treeinuri = "tcp://*:5556"

if pepe.rank == 0 then
    pepe.run ("echo bind 127.0.0.1 | /usr/sbin/redis-server -")
    pepe.run ("./cmbd --event-uri='" .. eventuri .. "'"
		.. " --tree-in-uri='" .. treeinuri .. "'"
		.. " --redis-server=localhost"
		.. " --rank=" .. pepe.rank
		.. " --size=" .. #h)
else
    local parent_rank = pepe.rank - 1
    local treeouturi = "tcp://" ..  h[parent_rank + 1] .. ":5556"
    pepe.run ("./cmbd --event-uri='" .. eventuri .. "'"
		.. " --tree-in-uri='" .. treeinuri .. "'"
		.. " --tree-out-uri='" .. treeouturi .. "'"
		.. " --rank=" .. pepe.rank
		.. " --size=" .. #h)
end