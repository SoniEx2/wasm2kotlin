package wasm_rt_impl;

import kotlin.reflect.KMutableProperty0
import kotlin.reflect.KFunction

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

    // size is unnecessary, but...
    fun put(offset: Int, bytes: ByteArray, size: Int) {
        // independent position
        val indep = mem.duplicate()
        indep.position(offset)
        indep.put(bytes, 0, size)
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
        return a.toFloat() + 18446744073709551616f
    }
    return a.toFloat()
}
fun ULongToDouble(a: Long): Double {
    if (a < 0L) {
        return a.toFloat() + 18446744073709551616.0
    }
    return a.toDouble()
}

fun I32_TRUNC_S_F32(a: Float ): Int  = throw WasmException("UNIMPLEMENTED")
fun I64_TRUNC_S_F32(a: Float ): Long = throw WasmException("UNIMPLEMENTED")
fun I32_TRUNC_S_F64(a: Double): Int  = throw WasmException("UNIMPLEMENTED")
fun I64_TRUNC_S_F64(a: Double): Long = throw WasmException("UNIMPLEMENTED")
fun I32_TRUNC_U_F32(a: Float ): Int  = throw WasmException("UNIMPLEMENTED")
fun I64_TRUNC_U_F32(a: Float ): Long = throw WasmException("UNIMPLEMENTED")
fun I32_TRUNC_U_F64(a: Double): Int  = throw WasmException("UNIMPLEMENTED")
fun I64_TRUNC_U_F64(a: Double): Long = throw WasmException("UNIMPLEMENTED")
