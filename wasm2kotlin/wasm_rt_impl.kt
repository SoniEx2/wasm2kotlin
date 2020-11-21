package wasm_rt_impl;

const val PAGE_SIZE: Int = 65536;

interface ExternalRef<T> {
    var value: T
}

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

fun allocate_table(table: ExternalRef<Table>, elements: Int, max_elements: Int) {
}

fun allocate_memory(memory: ExternalRef<Memory>, initial_pages: Int, max_pages: Int) {
}

fun register_func_type(num_params: Int, num_results: Int, vararg types: Any): Int {
        return -1;
}
