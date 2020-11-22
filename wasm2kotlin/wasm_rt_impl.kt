package wasm_rt_impl;

import kotlin.reflect.KMutableProperty

const val PAGE_SIZE: Int = 65536;

class Memory(initial_pages: Int, max_pages: Int) {
    private var mem: java.nio.ByteBuffer

    init {
        mem = java.nio.ByteBuffer.allocate(initial_pages * PAGE_SIZE);
    }

    // size is unnecessary, but...
    fun put(offset: Int, bytes: ByteArray, size: Int) {
        // independent position
        val indep = mem.duplicate()
        indep.position(offset)
        indep.put(bytes, 0, size)
    }
}

class Table(elements: Int, max_elements: Int) {
}

open class WasmException(message: String? = null, cause: Throwable? = null) : RuntimeException(message, cause) {
}

open class ExhaustionException(message: String? = null, cause: Throwable? = null) : WasmException(message, cause) {
}

fun allocate_table(table: KMutableProperty<Table>, elements: Int, max_elements: Int) {
}

fun allocate_memory(memory: KMutableProperty<Memory>, initial_pages: Int, max_pages: Int) {
}

fun register_func_type(num_params: Int, num_results: Int, vararg types: Any): Int {
        return -1;
}
