local rtt = require("rtt")

local cases = {
   { "Bool", true },
   { "Int8", -8 },
   { "UInt8", 8 },
   { "Int16", -16 },
   { "UInt16", 16 },
   { "Int32", -32 },
   { "UInt32", 32 },
   { "Int64", -64 },
   { "UInt64", 64 },
   { "Float32", 1.25 },
   { "Float64", 2.5 },
   { "Char", "Q" },
   { "String", "canonical" },
}

for _, case in ipairs(cases) do
   local type_name, value = case[1], case[2]
   local variable = rtt.Variable.new(type_name, value)
   assert(variable:getType() == type_name, "wrong RTT type for " .. type_name)
   assert(variable:tolua() == value, "Lua round trip failed for " .. type_name)
end

local void_ok = pcall(rtt.Variable.new, "Void")
assert(not void_ok, "Void variables must not be constructible")

local empty_char_ok = pcall(rtt.Variable.new, "Char", "")
assert(not empty_char_ok, "an empty Lua string must not construct a Char")

local legacy_names = {
   "bool", "char", "short", "ushort", "int", "uint", "long", "llong", "ullong",
   "float", "double", "string", "void", "int8", "uint8", "int16", "uint16",
   "int32", "uint32", "int64", "uint64",
}

for _, type_name in ipairs(legacy_names) do
   local ok = pcall(rtt.Variable.new, type_name)
   assert(not ok, "legacy RTT type is still accepted: " .. type_name)
end

print("canonical scalar type test passed")
