package wasm_rt;

public enum Impl {
	;

	public static interface ExternalRef<T> {
		T get();
		void set(T value);
	}
	public static interface ExternalRefInt {
		int get();
		void set(int value);
	}
	public static interface ExternalRefLong {
		long get();
		void set(long value);
	}
	public static interface ExternalRefFloat {
		float get();
		void set(float value);
	}
	public static interface ExternalRefDouble {
		double get();
		void set(double value);
	}

	public static class Memory {
		private java.nio.ByteBuffer data;
		public Memory(int initial_pages, int max_pages) {
		}
	}
	public static class Table {
		public Table(int elements, int max_elements) {
		}
	}

	public static void allocate_table(ExternalRef<Table> table, int elements, int max_elements) {
	}
	public static void allocate_memory(ExternalRef<Memory> memory, int initial_pages, int max_pages) {
	}
	public static int register_func_type(int num_params, int num_results, Class... types) {
		return -1;
	}

	private Impl() {}
}
