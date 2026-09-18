-- Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
--
-- This file is part of Beebium.
--
-- Beebium is free software: you can redistribute it and/or modify it under the terms of the
-- GNU General Public License as published by the Free Software Foundation, either version 3 of the
-- License, or (at your option) any later version. Beebium is distributed in the hope that it will
-- be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
-- FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
-- You should have received a copy of the GNU General Public License along with Beebium.
-- If not, see <https://www.gnu.org/licenses/>.
--
-- Reusable MAME BBC oracle harness (autoboot script). Contains no MAME code; it
-- drives MAME (an external GPL program) through its Lua engine.
-- Parameters come from environment variables so the same script serves any run:
--   HK_KEYS   : keystrokes to type via the natural keyboard (e.g. 'CHAIN"FLIP!"\n')
--   HK_DELAY  : seconds to wait after boot before typing        (default 3)
--   HK_RUN    : seconds to run after typing before dumping      (default 12)
--   HK_DUMPS  : comma list of hex ranges "addr:len" to dump     (e.g. "5000:16,FD:2")
--   HK_OUT    : path for the text dump                          (default harness_out.txt)
--   HK_SNAP   : if set (any value), save a PNG snapshot
--   HK_CPUTAG : maincpu device tag                              (default ":maincpu")
--   HK_SPACE  : address space name                             (default "program")
-- Validated against MAME v0.289 Lua API (luaengine*.cpp): emu.wait (yielding),
-- manager.machine.natkeyboard:post, devices[tag].spaces[name]:read_u8,
-- devices[tag].state[sym].value, machine.video:snapshot(), machine:exit().

local function getenv(n, d) local v = os.getenv(n); if v == nil or v == "" then return d end; return v end

local KEYS   = getenv("HK_KEYS", "")
local DELAY  = tonumber(getenv("HK_DELAY", "3"))
local RUN    = tonumber(getenv("HK_RUN", "12"))
local DUMPS  = getenv("HK_DUMPS", "")
local OUT    = getenv("HK_OUT", "harness_out.txt")
local SNAP   = os.getenv("HK_SNAP")
local CPUTAG = getenv("HK_CPUTAG", ":maincpu")
local SPACE  = getenv("HK_SPACE", "program")

local function main()
    -- Let the machine boot.
    emu.wait(DELAY)

    if #KEYS > 0 then
        -- \n in the env string means RETURN
        manager.machine.natkeyboard:post(KEYS)
    end

    emu.wait(RUN)

    local cpu = manager.machine.devices[CPUTAG]
    local mem = cpu.spaces[SPACE]
    local f = assert(io.open(OUT, "w"))

    local function reg(sym)
        local ok, e = pcall(function() return cpu.state[sym].value end)
        return ok and e or -1
    end
    f:write(string.format("PC=%04X A=%02X X=%02X Y=%02X S=%02X P=%02X\n",
        reg("PC") % 0x10000, reg("A") % 0x100, reg("X") % 0x100,
        reg("Y") % 0x100, reg("SP") % 0x100, reg("P") % 0x100))

    for range in string.gmatch(DUMPS, "[^,]+") do
        local a, l = range:match("^%s*(%x+):(%d+)%s*$")
        if a then
            a = tonumber(a, 16); l = tonumber(l)
            local bytes = {}
            for i = 0, l - 1 do
                bytes[#bytes + 1] = string.format("%02x", mem:read_u8(a + i))
            end
            f:write(string.format("&%04X: %s\n", a, table.concat(bytes, " ")))
        end
    end
    f:close()

    if SNAP then manager.machine.video:snapshot() end
    manager.machine:exit()
end

-- Autoboot scripts execute in a coroutine, so emu.wait() may yield here.
main()
