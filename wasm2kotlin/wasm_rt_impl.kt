package wasm_rt_impl;

import kotlin.reflect.KMutableProperty0

const val PAGE_SIZE: Int = 65536;

class Memory(initial_pages: Int, max_pages: Int) {
    private var mem: java.nio.ByteBuffer

    init {
        mem = java.nio.ByteBuffer.allocate(initial_pages * PAGE_SIZE);
        mem.order(java.nio.ByteOrder.LITTLE_ENDIAN);
    }

    // size is unnecessary, but...
    fun put(offset: Int, bytes: ByteArray, size: Int) {
        // independent position
        val indep = mem.duplicate()
        indep.position(offset)
        indep.put(bytes, 0, size)
    }

    fun i32_store(position: Long, value: Int) {
        mem.putInt(position.toInt(), value);
    }

    fun i32_load(position: Long): Int {
        return mem.getInt(position.toInt())
    }

    fun i64_store(position: Long, value: Long) {
        mem.putLong(position.toInt(), value)
    }

    fun i64_load(position: Long): Long {
        return mem.getLong(position.toInt())
    }
}

class Table(elements: Int, max_elements: Int) {
}

open class WasmException(message: String? = null, cause: Throwable? = null) : RuntimeException(message, cause) {
}

open class ExhaustionException(message: String? = null, cause: Throwable? = null) : WasmException(message, cause) {
}

open class UnreachableException(message: String? = null, cause: Throwable? = null) : WasmException(message, cause) {
}

fun allocate_table(table: KMutableProperty0<Table>, elements: Int, max_elements: Int) {
    table.set(Table(elements, max_elements))
}

fun allocate_memory(memory: KMutableProperty0<Memory>, initial_pages: Int, max_pages: Int) {
    memory.set(Memory(initial_pages, max_pages))
}

fun register_func_type(num_params: Int, num_results: Int, vararg types: Any): Int {
        return -1;
}

fun i32_store(memory: KMutableProperty0<Memory>, position: Long, value: Int) = memory.get().i32_store(position, value)
fun i32_load(memory: KMutableProperty0<Memory>, position: Long): Int = memory.get().i32_load(position)

fun i64_store(memory: KMutableProperty0<Memory>, position: Long, value: Long) = memory.get().i64_store(position, value)
fun i64_load(memory: KMutableProperty0<Memory>, position: Long): Long = memory.get().i64_load(position)

fun Boolean.btoInt(): Int = if (this) 1 else 0
fun Boolean.btoLong(): Long = if (this) 1L else 0L

fun Int.isz(): Boolean = this == 0
fun Long.isz(): Boolean = this == 0L
