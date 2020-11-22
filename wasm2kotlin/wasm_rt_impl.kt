package wasm_rt_impl;

import kotlin.reflect.KMutableProperty0

const val PAGE_SIZE: Int = 65536;

class Memory(initial_pages: Int, max_pages: Int) {
    private var mem: java.nio.ByteBuffer

    init {
        mem = java.nio.ByteBuffer.allocate(initial_pages * PAGE_SIZE);
        mem.order(java.nio.ByteOrder.LITTLE_ENDIAN);
    }

    // TODO(Soni): store pages elsewhere for performance
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
    private fun <T> protect(f: () -> T): T {
        try {
            return f()
        } catch(e: IndexOutOfBoundsException) {
            throw RangeException(null, e)
        }
    }

    fun i32_store(position: Long, value: Int)    { protect { mem.putInt(position.toInt(), value)    } }
    fun i64_store(position: Long, value: Long)   { protect { mem.putLong(position.toInt(), value)   } }
    fun f32_store(position: Long, value: Float)  { protect { mem.putFloat(position.toInt(), value)  } }
    fun f64_store(position: Long, value: Double) { protect { mem.putDouble(position.toInt(), value) } }

    fun i32_store8(position: Long, value: Int)   { protect { mem.put(position.toInt(), value.toByte())       } }
    fun i64_store8(position: Long, value: Long)  { protect { mem.put(position.toInt(), value.toByte())       } }
    fun i32_store16(position: Long, value: Int)  { protect { mem.putShort(position.toInt(), value.toShort()) } }
    fun i64_store16(position: Long, value: Long) { protect { mem.putShort(position.toInt(), value.toShort()) } }
    fun i64_store32(position: Long, value: Long) { protect { mem.putInt(position.toInt(), value.toInt())     } }

    fun i32_load(position: Long): Int    = protect { mem.getInt(position.toInt())    }
    fun i64_load(position: Long): Long   = protect { mem.getLong(position.toInt())   }
    fun f32_load(position: Long): Float  = protect { mem.getFloat(position.toInt())  }
    fun f64_load(position: Long): Double = protect { mem.getDouble(position.toInt()) }

    fun i32_load8_s(position: Long): Int   = protect { mem.get(position.toInt())      }.toInt()
    fun i64_load8_s(position: Long): Long  = protect { mem.get(position.toInt())      }.toLong() 
    fun i32_load8_u(position: Long): Int   = protect { mem.get(position.toInt())      }.toInt() and 0xFF
    fun i64_load8_u(position: Long): Long  = protect { mem.get(position.toInt())      }.toLong() and 0xFFL
    fun i32_load16_s(position: Long): Int  = protect { mem.getShort(position.toInt()) }.toInt()
    fun i64_load16_s(position: Long): Long = protect { mem.getShort(position.toInt()) }.toLong()
    fun i32_load16_u(position: Long): Int  = protect { mem.getShort(position.toInt()) }.toInt() and 0xFFFF
    fun i64_load16_u(position: Long): Long = protect { mem.getShort(position.toInt()) }.toLong() and 0xFFFFL
    fun i64_load32_s(position: Long): Long = protect { mem.getInt(position.toInt())   }.toLong()
    fun i64_load32_u(position: Long): Long = protect { mem.getInt(position.toInt())   }.toLong() and 0xFFFFFFFFL

    // NOTE: Not thread-safe.
    fun resize(new_pages: Int): Int {
        val old_mem = mem;
        val old_pages = pages;
        if (new_pages < 0 || new_pages > 65535) {
            return -1;
        }
        if (old_pages + new_pages > 65535) {
            return -1;
        }
        val total_pages = old_pages + new_pages;
        mem = java.nio.ByteBuffer.allocate(total_pages * PAGE_SIZE);
        mem.order(java.nio.ByteOrder.LITTLE_ENDIAN);
        mem.put(old_mem)
        return old_pages;
    }

}

class Table(elements: Int, max_elements: Int) {
}

open class WasmException(message: String? = null, cause: Throwable? = null) : RuntimeException(message, cause) {
}

open class ExhaustionException(message: String? = null, cause: Throwable? = null) : WasmException(message, cause) {
}

open class RangeException(message: String? = null, cause: Throwable? = null) : WasmException(message, cause) {
}

open class UnreachableException(message: String? = null, cause: Throwable? = null) : WasmException(message, cause) {
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

fun register_func_type(num_params: Int, num_results: Int, vararg types: Any): Int {
    //throw RuntimeException("unimplemented")
    return -1
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

fun Boolean.btoInt(): Int = if (this) 1 else 0
fun Boolean.btoLong(): Long = if (this) 1L else 0L

fun Int.isz(): Boolean = this == 0
fun Long.isz(): Boolean = this == 0L
fun Int.inz(): Boolean = this != 0
fun Long.inz(): Boolean = this != 0L
