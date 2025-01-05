-- WebAssembly interpreter for LuaJIT
-- requires dkjson for --spec

local ffi = nil
if jit then
    ffi = require "ffi"
end
if not bit then require "bit" end

local ssub = string.sub
local schar = string.char
local sbyte = string.byte
local blshift = bit.lshift
local min = math.min
local max = math.max
local select = select
-- forbid load/loadstring usage
local load, loadstring = nil

local M = {}

local function checkbuflen(state, len)
    local buf = state.buf
    if not buf then
        return nil
    end
    while #buf < len do
        local extra = state.reader()
        if not extra or extra == "" then
            return nil
        end
        buf = buf .. extra
        state.buf = buf
    end
    state.buf = ssub(buf, len + 1)
    return ssub(buf, 1, len)
end

local function checkbuf(state, prefix, reason)
    local buf = checkbuflen(state, #prefix)
    if not buf then
        return nil, "unexpected end"
    end
    if buf ~= prefix then
        return nil, reason
    end
    return buf
end

local function checkuleb128(state, maxlen)
    -- avoid using checkbuflen if we don't strictly have to
    -- TODO optimize (further)
    local buf = state.buf
    local target = 1
    local byte
    repeat
        byte = sbyte(buf, target, target)
        target = target + 1
        maxlen = maxlen - 7
    until (not byte) or byte < 128 or maxlen <= 0
    if byte then
        if byte < blshift(1, min(7, maxlen + 7)) then
            return checkbuflen(target)
        else
            return nil
        end
    end
    -- slow path
    error("NYI")
end

local function checksleb128(state, maxlen)
    -- avoid using checkbuflen if we don't strictly have to
    local buf = state.buf
    sbyte(buf, 1, 1)
end

local fast_drop_keep = {
    -- keep k values while dropping d-1 and leaving the rest unmodified
    -- top k3 k2 k1 d3 d2 d1 u3 u2 u1 bottom
    -- indexed by keep, max 200
    -- these are O(n)
    [0] = function(d)
        local continuation
        return function(...) return continuation(select(d, ...)) end,
        function(next) continuation = next end
    end,
}

local function slow_drop_keep_impl(k, d, ...)
    -- this is O(n^2) sorry
    if k == 0 then
        return select(d, ...)
    end
    return ..., slow_drop_keep_impl(k - 1, d, select(2, ...))
end

local function slow_drop_keep(k, d)
    local continuation
    return function(...)
        return continuation(slow_drop_keep_impl(k, d, ...))
    end,
    function(next) continuation = next end
end

local function emit_drop_keep(k, d)
    local factory = fast_drop_keep[k]
    if factory then
        return factory(d)
    else
        return slow_drop_keep(k, d)
    end
end

local fast_call = {
    -- fast_call can be implemented for up to 200 arguments, and up to 199
    -- results (we need to reserve one local for exception handling)
    -- that's more than enough for 99% of wasm
    -- (upvalues don't count for this)
    -- indexed by results, then arguments
    [0] = {
        [0] = function()
            local target, continuation
            return function(...)
                local ok, ex
                ok, ex = target()
                if not ok then return ok, ex end
                return continuation(...)
            end,
            function(link) target = link end,
            function(next) continuation = next end
        end,
    },
    [1] = {
        [0] = function()
            local target, continuation
            return function(...)
                local ok, ex
                ok, ex = target()
                if not ok then return ok, ex end
                return continuation(ex, ...)
            end,
            function(link) target = link end,
            function(next) continuation = next end
        end,
    },
}

local function slow_retain(n, ...)
    -- we can assume this is only ever called for n > 200
    -- this is very similar to slow_drop_keep but specialized
    if n == 0 then
        return
    end
    return ..., slow_retain(n - 1, select(2, ...))
end

local function slow_push(n, t, continuation, ...)
    if n == 0 then
        return continuation(...)
    end
    return slow_push(n - 1, t, continuation, t[n], ...)
end

local function check_or_pack(ok, ...)
    if not ok then return ok, (...) end
    return ok, {...}
end

local function slow_call(a, r)
    local target, continuation
    return function(...)
        -- we generally try to avoid tables since they are so slow
        -- but it's necessary here
        local ok, results = check_or_pack(target(slow_retain(a, ...)))
        if not ok then return ok, results end
        return slow_push(r, results, continuation, select(a + 1, ...))
    end,
    function(link) target = link end,
    function(next) continuation = next end
end

local function emit_call(a, r)
    local factories = fast_call[r]
    if factories then
        local factory = factories[a]
        if factory then
            return factory()
        end
    end
    return slow_call(a, r)
end

local section_names = {
    [0] = "custom",
    "type",
    "import",
    "function",
    "table",
    "memory",
    "global",
    "export",
    "start",
    "element",
    "code",
    "data",
    "data count",
}
local section_order = {
    ["type"] = 1,
    ["import"] = 2,
    ["function"] = 3,
    ["table"] = 4,
    ["memory"] = 5,
    ["global"] = 6,
    ["export"] = 7,
    ["start"] = 8,
    ["element"] = 9,
    ["data count"] = 10,
    ["code"] = 11,
    ["data"] = 12,
}

local function loadwasm(reader)
    local state = {buf = reader(), reader = reader}
    local ok, err
    ok, err = checkbuf(state, "\0asm", "magic header not detected")
    if not ok then
        return ok, err
    end
    ok, err = checkbuf(state, schar(1, 0, 0, 0), "unknown binary version")
    if not ok then
        return ok, err
    end
    local m = {}
    local last_section = 0
    while true do
        local section = checkbuflen(state, 1)
        if not section then break end
        section = section_names[sbyte(section)]
        if not section then
            return nil, "malformed section id"
        end
        if section ~= "custom" then
            if section_order[section] <= last_section then
                if section_order[section] == last_section then
                    return nil, "duplicate section"
                end
                return nil, "section out of order"
            end
            error("NYI")
        end
    end
    -- check postconditions
    return m
end

local function loadcomponent(reader)
    error("NYI")
end

if debug and debug.traceback and select(2, string.gsub(debug.traceback(), "\n", "\n")) == 2 then
    -- run spec tests
    local spec = false
    -- verbose
    local verbose = false
    -- the file to process/run
    local file = nil
    -- whether to accept further options
    local opts = true
    for i,v in ipairs(arg) do
        if opts and v:sub(1,1) == "-" then
            if v == "--spec" then
                if not pcall(require, "dkjson") then
                    io.stderr:write("--spec requires dkjson\n")
                    io.stderr:flush()
                    pcall(os.exit, false)
                    os.exit(1)
                    return
                end
                spec = true
            elseif v == "-v" or v == "--verbose" then
                verbose = true
                io.stderr:write("jit: ", jit and "true" or "false", "\n")
                io.stderr:write("ffi: ", ffi and "true" or "false", "\n")
                io.stderr:flush()
            elseif v == "-h" or v == "--help" then
                return
            elseif v == "--" then
                opts = false
            else
                io.stderr:write("unknown option: ", v, "\n")
                io.stderr:flush()
                pcall(os.exit, false)
                os.exit(1)
                return
            end
        else
            if not file then
                file = v
            else
                io.stderr:write("extraneous argument: ", v, "\n")
                io.stderr:flush()
                pcall(os.exit, false)
                os.exit(1)
                return
            end
        end
    end
    if not file then
        io.stderr:write("expected filename argument\n")
        io.stderr:flush()
        pcall(os.exit, false)
        os.exit(1)
        return
    end
    if verbose then
        io.stderr:write("processing ", (spec and "spec" or "wasm"),
                        " file: ", file, "\n")
        io.stderr:flush()
    end
    if spec then
        local dkjson = require "dkjson"
        local f = assert(io.open(file, "rb"))
        local jsondata = f:read("*a")
        local jsontable, eof = dkjson.decode(jsondata)
        if not jsontable or jsondata:sub(eof):find("%S") then
            io.stderr:write("invalid input: not a json file\n")
            io.stderr:flush()
            pcall(os.exit, false)
            os.exit(1)
            return
        end
        local base_dir = file:gsub("[-_a-zA-Z0-9.]+%.json$", "")
        local source_filename = jsontable.source_filename
        local commands = jsontable.commands
        local module
        local n = 0
        for i, command in ipairs(commands) do
            if command.type == "module" then
                -- FIXME error handling?
                local ok, err
                ok, module, err = pcall(loadwasm, assert(io.open(base_dir .. command.filename, "rb")):lines(8192))
                if ok and module then
                    n = n + 1
                else
                    print(source_filename, command.line, ok, module, err)
                end
            elseif command.type == "assert_malformed" then
                -- FIXME error handling?
                local ok, err
                ok, module, err = pcall(loadwasm, assert(io.open(base_dir .. command.filename, "rb")):lines(8192))
                if ok and not module and err == command.text then
                    n = n + 1
                else
                    print(source_filename, command.line, ok, module, err)
                end
            else
                print("unknown command: ", command.type, "\n")
            end
        end
        print(n.."/"..#commands.." tests passed")
    else
    end
else
    return M
end
