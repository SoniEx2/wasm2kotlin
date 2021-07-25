// Copyright 2020-2021 Soni L.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Based on wasm2c, under the following license notice:
//
// Copyright 2018 WebAssembly Community Group participants
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package wasm_rt_impl;

import kotlin.reflect.KMutableProperty0
import kotlin.reflect.KProperty0
import kotlin.Function

open class ModuleRegistry {
    private var funcs: HashMap<Pair<String, String>, Any> = HashMap<Pair<String, String>, Any>();
    private var tables: HashMap<Pair<String, String>, KMutableProperty0<Table>> = HashMap<Pair<String, String>, KMutableProperty0<Table>>();
    private var globals: HashMap<Pair<String, String>, Any> = HashMap<Pair<String, String>, Any>();
    private var constants: HashMap<Pair<String, String>, Any> = HashMap<Pair<String, String>, Any>();
    private var memories: HashMap<Pair<String, String>, KMutableProperty0<Memory>> = HashMap<Pair<String, String>, KMutableProperty0<Memory>>();

    open fun <T> exportFunc(modname: String, fieldname: String, value: Function<T>) {
        funcs.put(Pair(modname, fieldname), value)
    }
    open fun exportTable(modname: String, fieldname: String, value: KMutableProperty0<Table>) {
        tables.put(Pair(modname, fieldname), value)
    }
    open fun <T> exportGlobal(modname: String, fieldname: String, value: KMutableProperty0<T>) {
        globals.put(Pair(modname, fieldname), value)
    }
    open fun <T> exportConstant(modname: String, fieldname: String, value: KProperty0<T>) {
        constants.put(Pair(modname, fieldname), value)
    }
    open fun exportMemory(modname: String, fieldname: String, value: KMutableProperty0<Memory>) {
        memories.put(Pair(modname, fieldname), value)
    }

    // TODO add exceptions
    open fun <T: Function<U>, U> importFunc(modname: String, fieldname: String): T {
        return funcs.get(Pair(modname, fieldname)) as T
    }
    open fun importTable(modname: String, fieldname: String): KMutableProperty0<Table> {
        return tables.get(Pair(modname, fieldname)) as KMutableProperty0<Table>
    }
    open fun <T> importGlobal(modname: String, fieldname: String): KMutableProperty0<T> {
        return globals.get(Pair(modname, fieldname)) as KMutableProperty0<T>
    }
    open fun <T> importConstant(modname: String, fieldname: String): KProperty0<T> {
        return constants.get(Pair(modname, fieldname)) as KProperty0<T>
    }
    open fun importMemory(modname: String, fieldname: String): KMutableProperty0<Memory> {
        return memories.get(Pair(modname, fieldname)) as KMutableProperty0<Memory>
    }
}

const val PAGE_SIZE: Int = 65536;

class Memory(initial_pages: Int, max_pages: Int) {
    private val max_pages = max_pages

    private var mem: java.nio.ByteBuffer

    init {
        mem = java.nio.ByteBuffer.allocate(initial_pages * PAGE_SIZE);
        mem.order(java.nio.ByteOrder.LITTLE_ENDIAN);
    }

    // TODO(Soni): store pages somewhere for performance?
    val pages: Int
        get() = mem.capacity() / PAGE_SIZE;

    fun put(offset: Int, bytes_as_ucs2: String, size: Int) {
        val temp = mem.duplicate()
        // duplicate resets byte order
        temp.order(java.nio.ByteOrder.LITTLE_ENDIAN)
        temp.position(offset)
        val cb = temp.asCharBuffer();
        cb.put(bytes_as_ucs2, 0, size/2);
        if (size/2 != bytes_as_ucs2.length) {
            // size is odd, so we have an extra byte
            temp.put(offset+size-1, bytes_as_ucs2[size/2].toByte())
        }
    }

    // converts native index out of bounds into wasm2kotlin exceptions
    private inline fun <T> protect(f: () -> T): T {
        try {
            return f()
        } catch(e: IndexOutOfBoundsException) {
            throw RangeException(null, e)
        }
    }

    private fun offsetp(pos: Int, offset: Int): Int {
        val pos = (pos.toLong() and 0xFFFFFFFF) + (offset.toLong() and 0xFFFFFFFF);
        if ((pos and 0xFFFFFFFFL) != pos) {
            throw RangeException()
        }
        return pos.toInt()
    }

    fun i32_store(position: Int, offset: Int, value: Int)    { protect { mem.putInt(offsetp(position, offset), value)    } }
    fun i64_store(position: Int, offset: Int, value: Long)   { protect { mem.putLong(offsetp(position, offset), value)   } }
    fun f32_store(position: Int, offset: Int, value: Float)  { protect { mem.putFloat(offsetp(position, offset), value)  } }
    fun f64_store(position: Int, offset: Int, value: Double) { protect { mem.putDouble(offsetp(position, offset), value) } }

    fun i32_store8(position: Int, offset: Int, value: Int)   { protect { mem.put(offsetp(position, offset), value.toByte())       } }
    fun i64_store8(position: Int, offset: Int, value: Long)  { protect { mem.put(offsetp(position, offset), value.toByte())       } }
    fun i32_store16(position: Int, offset: Int, value: Int)  { protect { mem.putShort(offsetp(position, offset), value.toShort()) } }
    fun i64_store16(position: Int, offset: Int, value: Long) { protect { mem.putShort(offsetp(position, offset), value.toShort()) } }
    fun i64_store32(position: Int, offset: Int, value: Long) { protect { mem.putInt(offsetp(position, offset), value.toInt())     } }

    fun i32_load(position: Int, offset: Int): Int    = protect { mem.getInt(offsetp(position, offset))    }
    fun i64_load(position: Int, offset: Int): Long   = protect { mem.getLong(offsetp(position, offset))   }
    fun f32_load(position: Int, offset: Int): Float  = protect { mem.getFloat(offsetp(position, offset))  }
    fun f64_load(position: Int, offset: Int): Double = protect { mem.getDouble(offsetp(position, offset)) }

    fun i32_load8_s(position: Int, offset: Int): Int   = protect { mem.get(offsetp(position, offset))      }.toInt()
    fun i64_load8_s(position: Int, offset: Int): Long  = protect { mem.get(offsetp(position, offset))      }.toLong()
    fun i32_load8_u(position: Int, offset: Int): Int   = protect { mem.get(offsetp(position, offset))      }.toInt() and 0xFF
    fun i64_load8_u(position: Int, offset: Int): Long  = protect { mem.get(offsetp(position, offset))      }.toLong() and 0xFFL
    fun i32_load16_s(position: Int, offset: Int): Int  = protect { mem.getShort(offsetp(position, offset)) }.toInt()
    fun i64_load16_s(position: Int, offset: Int): Long = protect { mem.getShort(offsetp(position, offset)) }.toLong()
    fun i32_load16_u(position: Int, offset: Int): Int  = protect { mem.getShort(offsetp(position, offset)) }.toInt() and 0xFFFF
    fun i64_load16_u(position: Int, offset: Int): Long = protect { mem.getShort(offsetp(position, offset)) }.toLong() and 0xFFFFL
    fun i64_load32_s(position: Int, offset: Int): Long = protect { mem.getInt(offsetp(position, offset))   }.toLong()
    fun i64_load32_u(position: Int, offset: Int): Long = protect { mem.getInt(offsetp(position, offset))   }.toLong() and 0xFFFFFFFFL

    fun i32_store(position: Int, value: Int)    { protect { mem.putInt(position, value)    } }
    fun i64_store(position: Int, value: Long)   { protect { mem.putLong(position, value)   } }
    fun f32_store(position: Int, value: Float)  { protect { mem.putFloat(position, value)  } }
    fun f64_store(position: Int, value: Double) { protect { mem.putDouble(position, value) } }

    fun i32_store8(position: Int, value: Int)   { protect { mem.put(position, value.toByte())       } }
    fun i64_store8(position: Int, value: Long)  { protect { mem.put(position, value.toByte())       } }
    fun i32_store16(position: Int, value: Int)  { protect { mem.putShort(position, value.toShort()) } }
    fun i64_store16(position: Int, value: Long) { protect { mem.putShort(position, value.toShort()) } }
    fun i64_store32(position: Int, value: Long) { protect { mem.putInt(position, value.toInt())     } }

    fun i32_load(position: Int): Int    = protect { mem.getInt(position)    }
    fun i64_load(position: Int): Long   = protect { mem.getLong(position)   }
    fun f32_load(position: Int): Float  = protect { mem.getFloat(position)  }
    fun f64_load(position: Int): Double = protect { mem.getDouble(position) }

    fun i32_load8_s(position: Int): Int   = protect { mem.get(position)      }.toInt()
    fun i64_load8_s(position: Int): Long  = protect { mem.get(position)      }.toLong()
    fun i32_load8_u(position: Int): Int   = protect { mem.get(position)      }.toInt() and 0xFF
    fun i64_load8_u(position: Int): Long  = protect { mem.get(position)      }.toLong() and 0xFFL
    fun i32_load16_s(position: Int): Int  = protect { mem.getShort(position) }.toInt()
    fun i64_load16_s(position: Int): Long = protect { mem.getShort(position) }.toLong()
    fun i32_load16_u(position: Int): Int  = protect { mem.getShort(position) }.toInt() and 0xFFFF
    fun i64_load16_u(position: Int): Long = protect { mem.getShort(position) }.toLong() and 0xFFFFL
    fun i64_load32_s(position: Int): Long = protect { mem.getInt(position)   }.toLong()
    fun i64_load32_u(position: Int): Long = protect { mem.getInt(position)   }.toLong() and 0xFFFFFFFFL

    // NOTE: Not thread-safe.
    fun resize(new_pages: Int): Int {
        val old_mem = mem;
        val old_pages = pages;
        if (new_pages < 0 || new_pages > 65536) {
            return -1;
        }
        if (old_pages + new_pages > max_pages) {
            return -1;
        }
        val total_pages = old_pages + new_pages;
        mem = java.nio.ByteBuffer.allocate(total_pages * PAGE_SIZE);
        mem.order(java.nio.ByteOrder.LITTLE_ENDIAN);
        // NOTE: duplicate resets byte order but it's fine here
        mem.duplicate().put(old_mem)
        return old_pages;
    }

}

class Table(elements: Int, max_elements: Int) {
    private val max_elems = max_elements;
    private var elems: ArrayList<Elem?> = ArrayList<Elem?>();

    init {
        while (elems.size < elements) {
            elems.add(null);
        }
    }

    operator fun get(i: Int): Elem {
        try {
            return elems.get(i) as Elem
        } catch (e: IndexOutOfBoundsException) {
            throw RangeException(null, e)
        }
    }
    operator fun set(i: Int, value: Elem) {
        try {
            elems.set(i, value)
        } catch (e: IndexOutOfBoundsException) {
            throw RangeException(null, e)
        }
    }
}

data class Elem(val type: Int, val func: Function<out Any>) {
}

open class WasmException(message: String? = null, cause: Throwable? = null) : RuntimeException(message, cause) {
}

open class ExhaustionException(message: String? = null, cause: Throwable? = null) : WasmException(message, cause) {
}

open class RangeException(message: String? = null, cause: Throwable? = null) : WasmException(message, cause) {
}

open class UnreachableException(message: String? = null, cause: Throwable? = null) : WasmException(message, cause) {
}

open class CallIndirectException(message: String? = null, cause: Throwable? = null) : WasmException(message, cause) {
}

open class DivByZeroException(message: String? = null, cause: Throwable? = null) : WasmException(message, cause) {
}

open class IntOverflowException(message: String? = null, cause: Throwable? = null) : WasmException(message, cause) {
}

open class InvalidConversionException(message: String? = null, cause: Throwable? = null) : WasmException(message, cause) {
}

fun allocate_table(table: KMutableProperty0<Table>, elements: Int, max_elements: Int) {
    table.set(Table(elements, max_elements))
}

fun allocate_memory(memory: KMutableProperty0<Memory>, initial_pages: Int, max_pages: Int) {
    memory.set(Memory(initial_pages, max_pages))
}

fun grow_memory(memory: KMutableProperty0<Memory>, add_pages: Int): Int {
    return memory.get().resize(add_pages)
}

private var func_types_by_nresults: HashMap<Pair<Int, List<Any>>, Int> = HashMap<Pair<Int, List<Any>>, Int>()

fun register_func_type(num_params: Int, num_results: Int, vararg types: Any): Int {
    assert(num_params + num_results == types.size)
    synchronized (func_types_by_nresults) {
        val maybe_id: Int = func_types_by_nresults.size
        val id: Int = func_types_by_nresults.getOrPut(Pair(num_results, types.toList())) { maybe_id }
        return id
    }
}

// NOTE(Soni): these are inline not for "performance" but for code size.
// kept running into "Method too large", this should help with *some* of them.
inline fun Boolean.btoInt(): Int = if (this) 1 else 0
inline fun Boolean.btoLong(): Long = if (this) 1L else 0L

inline fun Int.isz(): Int = if (this == 0) 1 else 0
inline fun Long.isz(): Int = if (this == 0L) 1 else 0
inline fun Int.inz(): Boolean = this != 0
inline fun Long.inz(): Boolean = this != 0L

// NOTE(Soni): to preserve order of evaluation
inline fun <T> select(third: T, second: T, first: Int): T = if (first != 0) third else second

fun <T> CALL_INDIRECT(table: Table, type: Int, func: Int): T {
    val elem = try {
        table[func]
    } catch (e: IndexOutOfBoundsException) {
        throw CallIndirectException()
    } catch (e: NullPointerException) {
        throw CallIndirectException()
    }
    if (elem.type == type) {
        return elem.func as T
    } else {
        throw CallIndirectException()
    }
}

fun I32_DIV_S(a: Int, b: Int): Int {
    if (a == Int.MIN_VALUE && b == -1) { throw IntOverflowException() }
    try { return a/b }
    catch (e: ArithmeticException) { throw DivByZeroException() }
}
fun I64_DIV_S(a: Long, b: Long): Long {
    if (a == Long.MIN_VALUE && b == -1L) { throw IntOverflowException() }
    try { return a/b }
    catch (e: ArithmeticException) { throw DivByZeroException() }
}

fun I32_REM_S(a: Int, b: Int): Int {
    if (a == Int.MIN_VALUE && b == -1) { return 0 }
    try { return a%b }
    catch (e: ArithmeticException) { throw DivByZeroException() }
}
fun I64_REM_S(a: Long, b: Long): Long {
    if (a == Long.MIN_VALUE && b == -1L) { return 0L }
    try { return a%b }
    catch (e: ArithmeticException) { throw DivByZeroException() }
}

fun DIV_U(a: Int, b: Int): Int {
    try { return java.lang.Integer.divideUnsigned(a, b) }
    catch (e: ArithmeticException) { throw DivByZeroException() }
}
fun DIV_U(a: Long, b: Long): Long {
    try { return java.lang.Long.divideUnsigned(a, b) }
    catch (e: ArithmeticException) { throw DivByZeroException() }
}

fun REM_U(a: Int, b: Int): Int {
    try { return java.lang.Integer.remainderUnsigned(a, b) }
    catch (e: ArithmeticException) { throw DivByZeroException() }
}
fun REM_U(a: Long, b: Long): Long {
    try { return java.lang.Long.remainderUnsigned(a, b) }
    catch (e: ArithmeticException) { throw DivByZeroException() }
}

fun UIntToFloat(a: Int): Float {
    return (a.toLong() and 0xFFFFFFFFL).toFloat()
}
fun UIntToDouble(a: Int): Double {
    return (a.toLong() and 0xFFFFFFFFL).toDouble()
}
fun ULongToFloat(a: Long): Float {
    if (a < 0L) {
        val b = ((a shl 1) ushr 40).toInt();
        val ismiddle = (a and 0xFFFFFFFFFFL) == 0x8000000000L;
        if (ismiddle) {
            return Float.fromBits(((b ushr 1) or 0x5f000000) + ((b and 2) ushr 1))
        }
        return Float.fromBits(((b ushr 1) or 0x5f000000) + (b and 1))
    }
    return a.toFloat()
}
fun ULongToDouble(a: Long): Double {
    if (a < 0L) {
        val b = (a shl 1) ushr 11;
        val ismiddle = (a and 0x7FFL) == 0x400L;
        if (ismiddle) {
            return Double.fromBits(((b ushr 1) or 0x43e0000000000000L) + ((b and 2) ushr 1))
        }
        return Double.fromBits(((b ushr 1) or 0x43e0000000000000L) + (b and 1))
    }
    return a.toDouble()
}

fun I32_ROTL(x: Int , y: Int ) = (((x) shl ((y) and (31))) or ((x) ushr (((31) - (y) + 1) and (31))))
fun I64_ROTL(x: Long, y: Long) = (((x) shl ((y.toInt()) and (63))) or ((x) ushr (((63) - (y.toInt()) + 1) and (63))))
fun I32_ROTR(x: Int , y: Int ) = (((x) ushr ((y) and (31))) or ((x) shl (((31) - (y) + 1) and (31))))
fun I64_ROTR(x: Long, y: Long) = (((x) ushr ((y.toInt()) and (63))) or ((x) shl (((63) - (y.toInt()) + 1) and (63))))

fun I32_TRUNC_SAT_U_F32(x: Float ): Int  =
  if (x.isNaN()) { 0 }
  else if (x <= -1.0f) { 0 }
  else if (x >= 4294967296f) { -1 }
  else { x.toLong().toInt() }
fun I64_TRUNC_SAT_U_F32(x: Float ): Long =
  if (x.isNaN()) { 0L }
  else if (x <= -1.0f) { 0L }
  else if (x >= 18446744073709551616f) { -1L }
  else if (x < Long.MAX_VALUE.toFloat()) { x.toLong() }
  else { (x - Long.MAX_VALUE.toFloat()).toLong() + Long.MIN_VALUE }
fun I32_TRUNC_SAT_U_F64(x: Double): Int  =
  if (x.isNaN()) { 0 }
  else if (x <= -1.0) { 0 }
  else if (x >= 4294967296.0) { -1 }
  else { x.toLong().toInt() }
fun I64_TRUNC_SAT_U_F64(x: Double): Long =
  if (x.isNaN()) { 0L }
  else if (x <= -1.0) { 0L }
  else if (x >= 18446744073709551616.0) { -1L }
  else if (x < Long.MAX_VALUE.toDouble()) { x.toLong() }
  else { (x - Long.MAX_VALUE.toDouble()).toLong() + Long.MIN_VALUE }

fun I32_TRUNC_S_F32(x: Float ): Int  =
  if (x.isNaN()) { throw InvalidConversionException() }
  else if (!(x >= Int.MIN_VALUE.toFloat() && x < 2147483648f)) { throw IntOverflowException() }
  else { x.toInt() }
fun I64_TRUNC_S_F32(x: Float ): Long =
  if (x.isNaN()) { throw InvalidConversionException() }
  else if (!(x >= Long.MIN_VALUE.toFloat() && x < Long.MAX_VALUE.toFloat())) { throw IntOverflowException() }
  else { x.toLong() }
fun I32_TRUNC_S_F64(x: Double): Int  =
  if (x.isNaN()) { throw InvalidConversionException() }
  else if (!(x > -2147483649.0 && x < 2147483648.0)) { throw IntOverflowException() }
  else { x.toInt() }
fun I64_TRUNC_S_F64(x: Double): Long =
  if (x.isNaN()) { throw InvalidConversionException() }
  else if (!(x >= Long.MIN_VALUE.toDouble() && x < Long.MAX_VALUE.toDouble())) { throw IntOverflowException() }
  else { x.toLong() }

fun I32_TRUNC_U_F32(x: Float ): Int  =
  if (x.isNaN()) { throw InvalidConversionException() }
  else if (!(x > -1.0f && x < 4294967296f)) { throw IntOverflowException() }
  else { x.toLong().toInt() }
fun I64_TRUNC_U_F32(x: Float ): Long =
  if (x.isNaN()) { throw InvalidConversionException() }
  else if (!(x > -1.0f && x < 18446744073709551616f)) { throw IntOverflowException() }
  else if (x < Long.MAX_VALUE.toFloat()) { x.toLong() }
  else { (x - Long.MAX_VALUE.toFloat()).toLong() + Long.MIN_VALUE }
fun I32_TRUNC_U_F64(x: Double): Int  =
  if (x.isNaN()) { throw InvalidConversionException() }
  else if (!(x > -1.0 && x < 4294967296.0)) { throw IntOverflowException() }
  else { x.toLong().toInt() }
fun I64_TRUNC_U_F64(x: Double): Long =
  if (x.isNaN()) { throw InvalidConversionException() }
  else if (!(x > -1.0 && x < 18446744073709551616.0)) { throw IntOverflowException() }
  else if (x < Long.MAX_VALUE.toDouble()) { x.toLong() }
  else { (x - Long.MAX_VALUE.toDouble()).toLong() + Long.MIN_VALUE }

// math.min/max/floor/ceil/trunc don't canonicalize NaNs
// but wasm does
fun MIN(a: Float, b: Float): Float = Float.fromBits(kotlin.math.min(a, b).toBits())
fun MAX(a: Float, b: Float): Float = Float.fromBits(kotlin.math.max(a, b).toBits())
fun MIN(a: Double, b: Double): Double = Double.fromBits(kotlin.math.min(a, b).toBits())
fun MAX(a: Double, b: Double): Double = Double.fromBits(kotlin.math.max(a, b).toBits())

fun floor(x: Float): Float = Float.fromBits(kotlin.math.floor(x).toBits())
fun ceil(x: Float): Float = Float.fromBits(kotlin.math.ceil(x).toBits())
fun floor(x: Double): Double = Double.fromBits(kotlin.math.floor(x).toBits())
fun ceil(x: Double): Double = Double.fromBits(kotlin.math.ceil(x).toBits())

fun truncate(x: Float): Float = Float.fromBits(kotlin.math.truncate(x).toBits())
fun truncate(x: Double): Double = Double.fromBits(kotlin.math.truncate(x).toBits())

// JVM docs say Math.abs is equivalent to these
// but it doesn't hold for NaNs for some reason
fun abs(x: Double): Double = Double.fromBits(x.toRawBits() and Long.MAX_VALUE)
fun abs(x: Float): Float = Float.fromBits(x.toRawBits() and Int.MAX_VALUE)
