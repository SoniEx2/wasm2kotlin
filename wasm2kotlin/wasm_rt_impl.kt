package wasm_rt_impl;

import kotlin.reflect.KMutableProperty0
import kotlin.reflect.KFunction

class ModuleRegistry {
    private var funcs: HashMap<Pair<String, String>, Any> = HashMap<Pair<String, String>, Any>();
    private var tables: HashMap<Pair<String, String>, KMutableProperty0<Table>> = HashMap<Pair<String, String>, KMutableProperty0<Table>>();
    private var globals: HashMap<Pair<String, String>, Any> = HashMap<Pair<String, String>, Any>();
    private var memories: HashMap<Pair<String, String>, KMutableProperty0<Memory>> = HashMap<Pair<String, String>, KMutableProperty0<Memory>>();

    fun <T> exportFunc(modname: String, fieldname: String, value: KMutableProperty0<T>) {
        funcs.put(Pair(modname, fieldname), value)
    }
    fun exportTable(modname: String, fieldname: String, value: KMutableProperty0<Table>) {
        tables.put(Pair(modname, fieldname), value)
    }
    fun <T> exportGlobal(modname: String, fieldname: String, value: KMutableProperty0<T>) {
        globals.put(Pair(modname, fieldname), value)
    }
    fun exportMemory(modname: String, fieldname: String, value: KMutableProperty0<Memory>) {
        memories.put(Pair(modname, fieldname), value)
    }

    // TODO add exceptions
    fun <T> importFunc(modname: String, fieldname: String): KMutableProperty0<T> {
        return funcs.get(Pair(modname, fieldname)) as KMutableProperty0<T>
    }
    fun importTable(modname: String, fieldname: String): KMutableProperty0<Table> {
        return tables.get(Pair(modname, fieldname)) as KMutableProperty0<Table>
    }
    fun <T> importGlobal(modname: String, fieldname: String): KMutableProperty0<T> {
        return globals.get(Pair(modname, fieldname)) as KMutableProperty0<T>
    }
    fun importMemory(modname: String, fieldname: String): KMutableProperty0<Memory> {
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
            temp.put(size-1, bytes_as_ucs2[size/2].toByte())
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

    private fun intpos(pos: Long): Int {
        if ((pos and 0xFFFFFFFFL) != pos) {
            throw RangeException()
        }
        return pos.toInt()
    }

    fun i32_store(position: Long, value: Int)    { protect { mem.putInt(intpos(position), value)    } }
    fun i64_store(position: Long, value: Long)   { protect { mem.putLong(intpos(position), value)   } }
    fun f32_store(position: Long, value: Float)  { protect { mem.putFloat(intpos(position), value)  } }
    fun f64_store(position: Long, value: Double) { protect { mem.putDouble(intpos(position), value) } }

    fun i32_store8(position: Long, value: Int)   { protect { mem.put(intpos(position), value.toByte())       } }
    fun i64_store8(position: Long, value: Long)  { protect { mem.put(intpos(position), value.toByte())       } }
    fun i32_store16(position: Long, value: Int)  { protect { mem.putShort(intpos(position), value.toShort()) } }
    fun i64_store16(position: Long, value: Long) { protect { mem.putShort(intpos(position), value.toShort()) } }
    fun i64_store32(position: Long, value: Long) { protect { mem.putInt(intpos(position), value.toInt())     } }

    fun i32_load(position: Long): Int    = protect { mem.getInt(intpos(position))    }
    fun i64_load(position: Long): Long   = protect { mem.getLong(intpos(position))   }
    fun f32_load(position: Long): Float  = protect { mem.getFloat(intpos(position))  }
    fun f64_load(position: Long): Double = protect { mem.getDouble(intpos(position)) }

    fun i32_load8_s(position: Long): Int   = protect { mem.get(intpos(position))      }.toInt()
    fun i64_load8_s(position: Long): Long  = protect { mem.get(intpos(position))      }.toLong() 
    fun i32_load8_u(position: Long): Int   = protect { mem.get(intpos(position))      }.toInt() and 0xFF
    fun i64_load8_u(position: Long): Long  = protect { mem.get(intpos(position))      }.toLong() and 0xFFL
    fun i32_load16_s(position: Long): Int  = protect { mem.getShort(intpos(position)) }.toInt()
    fun i64_load16_s(position: Long): Long = protect { mem.getShort(intpos(position)) }.toLong()
    fun i32_load16_u(position: Long): Int  = protect { mem.getShort(intpos(position)) }.toInt() and 0xFFFF
    fun i64_load16_u(position: Long): Long = protect { mem.getShort(intpos(position)) }.toLong() and 0xFFFFL
    fun i64_load32_s(position: Long): Long = protect { mem.getInt(intpos(position))   }.toLong()
    fun i64_load32_u(position: Long): Long = protect { mem.getInt(intpos(position))   }.toLong() and 0xFFFFFFFFL

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

data class Elem(val type: Int, val func: KMutableProperty0<out Any>) {
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

fun i32_store(memory: KMutableProperty0<Memory>, position: Long, value: Int) = memory.get().i32_store(position, value)
fun i64_store(memory: KMutableProperty0<Memory>, position: Long, value: Long) = memory.get().i64_store(position, value)
fun f32_store(memory: KMutableProperty0<Memory>, position: Long, value: Float) = memory.get().f32_store(position, value)
fun f64_store(memory: KMutableProperty0<Memory>, position: Long, value: Double) = memory.get().f64_store(position, value)

fun i32_store8(memory: KMutableProperty0<Memory>, position: Long, value: Int)   = memory.get().i32_store8(position, value)
fun i64_store8(memory: KMutableProperty0<Memory>, position: Long, value: Long)  = memory.get().i64_store8(position, value)
fun i32_store16(memory: KMutableProperty0<Memory>, position: Long, value: Int)  = memory.get().i32_store16(position, value)
fun i64_store16(memory: KMutableProperty0<Memory>, position: Long, value: Long) = memory.get().i64_store16(position, value)
fun i64_store32(memory: KMutableProperty0<Memory>, position: Long, value: Long) = memory.get().i64_store32(position, value)

fun i32_load(memory: KMutableProperty0<Memory>, position: Long): Int = memory.get().i32_load(position)
fun i64_load(memory: KMutableProperty0<Memory>, position: Long): Long = memory.get().i64_load(position)
fun f32_load(memory: KMutableProperty0<Memory>, position: Long): Float = memory.get().f32_load(position)
fun f64_load(memory: KMutableProperty0<Memory>, position: Long): Double = memory.get().f64_load(position)

fun i32_load8_s(memory: KMutableProperty0<Memory>, position: Long): Int   = memory.get().i32_load8_s(position)
fun i64_load8_s(memory: KMutableProperty0<Memory>, position: Long): Long  = memory.get().i64_load8_s(position)
fun i32_load8_u(memory: KMutableProperty0<Memory>, position: Long): Int   = memory.get().i32_load8_u(position)
fun i64_load8_u(memory: KMutableProperty0<Memory>, position: Long): Long  = memory.get().i64_load8_u(position)
fun i32_load16_s(memory: KMutableProperty0<Memory>, position: Long): Int  = memory.get().i32_load16_s(position)
fun i64_load16_s(memory: KMutableProperty0<Memory>, position: Long): Long = memory.get().i64_load16_s(position)
fun i32_load16_u(memory: KMutableProperty0<Memory>, position: Long): Int  = memory.get().i32_load16_u(position)
fun i64_load16_u(memory: KMutableProperty0<Memory>, position: Long): Long = memory.get().i64_load16_u(position)
fun i64_load32_s(memory: KMutableProperty0<Memory>, position: Long): Long = memory.get().i64_load32_s(position)
fun i64_load32_u(memory: KMutableProperty0<Memory>, position: Long): Long = memory.get().i64_load32_u(position)

// NOTE(Soni): these are inline not for "performance" but for code size.
// kept running into "Method too large", this should help with *some* of them.
inline fun Boolean.btoInt(): Int = if (this) 1 else 0
inline fun Boolean.btoLong(): Long = if (this) 1L else 0L

inline fun Int.isz(): Boolean = this == 0
inline fun Long.isz(): Boolean = this == 0L
inline fun Int.inz(): Boolean = this != 0
inline fun Long.inz(): Boolean = this != 0L

fun <T> CALL_INDIRECT(table: Table, type: Int, func: Int): T {
    val elem = try {
        table[func]
    } catch (e: IndexOutOfBoundsException) {
        throw CallIndirectException()
    } catch (e: NullPointerException) {
        throw CallIndirectException()
    }
    if (elem.type == type) {
        return elem.func.get() as T
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
