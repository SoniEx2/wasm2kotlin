	public static int g_tests_run;
	public static int g_tests_passed;

	static void error(String file, int line, String format, Object[] args) {
	  //fprintf(stderr, "%s:%d: assertion failed: ", file, line);
	  //vfprintf(stderr, format, args);
	}

//#define ASSERT_TRAP(f)                                         \
//	  do {                                                         \
//	    g_tests_run++;                                             \
//	    if (wasm_rt.Impl_try() != 0) {                             \
//	      g_tests_passed++;                                        \
//	    } else {                                                   \
//	      (void)(f);                                               \
//	      error(__FILE__, __LINE__, "expected " #f " to trap.\n"); \
//	    }                                                          \
//	  } while (0)
//
//#define ASSERT_EXHAUSTION(f)                                     \
//	  do {                                                           \
//	    g_tests_run++;                                               \
//	    wasm_rt_trap_t code = wasm_rt.Impl_try();                    \
//	    switch (code) {                                              \
//	      case WASM_RT_TRAP_NONE:                                    \
//		(void)(f);                                               \
//		error(__FILE__, __LINE__, "expected " #f " to trap.\n"); \
//		break;                                                   \
//	      case WASM_RT_TRAP_EXHAUSTION:                              \
//		g_tests_passed++;                                        \
//		break;                                                   \
//	      default:                                                   \
//		error(__FILE__, __LINE__,                                \
//		      "expected " #f                                     \
//		      " to trap due to exhaustion, got trap code %d.\n", \
//		      code);                                             \
//		break;                                                   \
//	    }                                                            \
//	  } while (0)
//
//#define ASSERT_RETURN(f)                           \
//	  do {                                             \
//	    g_tests_run++;                                 \
//	    if (wasm_rt.Impl_try() != 0) {                 \
//	      error(__FILE__, __LINE__, #f " trapped.\n"); \
//	    } else {                                       \
//	      f;                                           \
//	      g_tests_passed++;                            \
//	    }                                              \
//	  } while (0)
//
//#define ASSERT_RETURN_T(type, fmt, f, expected)                          \
//	  do {                                                                   \
//	    g_tests_run++;                                                       \
//	    if (wasm_rt.Impl_try() != 0) {                                       \
//	      error(__FILE__, __LINE__, #f " trapped.\n");                       \
//	    } else {                                                             \
//	      type actual = f;                                                   \
//	      if (is_equal_##type(actual, expected)) {                           \
//		g_tests_passed++;                                                \
//	      } else {                                                           \
//		error(__FILE__, __LINE__,                                        \
//		      "in " #f ": expected %" fmt ", got %" fmt ".\n", expected, \
//		      actual);                                                   \
//	      }                                                                  \
//	    }                                                                    \
//	  } while (0)
//
//#define ASSERT_RETURN_NAN_T(type, itype, fmt, f, kind)                        \
//	  do {                                                                        \
//	    g_tests_run++;                                                            \
//	    if (wasm_rt.Impl_try() != 0) {                                            \
//	      error(__FILE__, __LINE__, #f " trapped.\n");                            \
//	    } else {                                                                  \
//	      type actual = f;                                                        \
//	      itype iactual;                                                          \
//	      memcpy(&iactual, &actual, sizeof(iactual));                             \
//	      if (is_##kind##_nan_##type(iactual)) {                                  \
//		g_tests_passed++;                                                     \
//	      } else {                                                                \
//		error(__FILE__, __LINE__,                                             \
//		      "in " #f ": expected result to be a " #kind " nan, got 0x%" fmt \
//		      ".\n",                                                          \
//		      iactual);                                                       \
//	      }                                                                       \
//	    }                                                                         \
//	  } while (0)
//
//#define ASSERT_RETURN_I32(f, expected) ASSERT_RETURN_T(u32, "u", f, expected)
//#define ASSERT_RETURN_I64(f, expected) ASSERT_RETURN_T(u64, PRIu64, f, expected)
//#define ASSERT_RETURN_F32(f, expected) ASSERT_RETURN_T(f32, ".9g", f, expected)
//#define ASSERT_RETURN_F64(f, expected) ASSERT_RETURN_T(f64, ".17g", f, expected)
//
//#define ASSERT_RETURN_CANONICAL_NAN_F32(f) \
//	  ASSERT_RETURN_NAN_T(f32, u32, "08x", f, canonical)
//#define ASSERT_RETURN_CANONICAL_NAN_F64(f) \
//	  ASSERT_RETURN_NAN_T(f64, u64, "016x", f, canonical)
//#define ASSERT_RETURN_ARITHMETIC_NAN_F32(f) \
//	  ASSERT_RETURN_NAN_T(f32, u32, "08x", f, arithmetic)
//#define ASSERT_RETURN_ARITHMETIC_NAN_F64(f) \
//	  ASSERT_RETURN_NAN_T(f64, u64, "016x", f, arithmetic)
//
	static boolean is_equal_u32(int x, int y) {
	  return x == y;
	}

	static boolean is_equal_u64(long x, long y) {
	  return x == y;
	}

	static boolean is_equal_f32(float x, float y) {
	  int ux, uy;
	  ux = Float.floatToRawIntBits(x);
	  uy = Float.floatToRawIntBits(y);
	  return ux == uy;
	}

	static boolean is_equal_f64(double x, double y) {
	  long ux, uy;
	  ux = Double.doubleToRawLongBits(x);
	  uy = Double.doubleToRawLongBits(y);
	  return ux == uy;
	}

	static float make_nan_f32(int x) {
	  x |= 0x7f800000;
	  float res;
	  res = Float.intBitsToFloat(x);
	  return res;
	}

	static double make_nan_f64(long x) {
	  x |= 0x7ff0000000000000l;
	  double res;
	  res = Double.longBitsToDouble(x);
	  return res;
	}

	static boolean is_canonical_nan_f32(int x) {
	  return (x & 0x7fffffff) == 0x7fc00000;
	}

	static boolean is_canonical_nan_f64(long x) {
	  return (x & 0x7fffffffffffffffl) == 0x7ff8000000000000l;
	}

	static boolean is_arithmetic_nan_f32(int x) {
	  return (x & 0x7fc00000) == 0x7fc00000;
	}

	static boolean is_arithmetic_nan_f64(long x) {
	  return (x & 0x7ff8000000000000l) == 0x7ff8000000000000l;
	}


	/*
	 * spectest implementations
	 */
	static void spectest_print() {
	  System.out.print("spectest.print()\n");
	}

	static void spectest_print_i32(int i) {
	  System.out.print("spectest.print_i32(");
	  System.out.print(i);
	  System.out.print(")\n");
	}

	static void spectest_print_f32(float f) {
	  //printf("spectest.print_f32(%g)\n", f);
	  System.out.print("spectest.print_f32(");
	  System.out.print(f);
	  System.out.print(")\n");
	}

	static void spectest_print_i32_f32(int i, float f) {
	  //printf("spectest.print_i32_f32(%d %g)\n", i, f);
	  System.out.print("spectest.print_i32_f32(");
	  System.out.print(i);
	  System.out.print(" ");
	  System.out.print(f);
	  System.out.print(")\n");
	}

	static void spectest_print_f64(double d) {
	  //printf("spectest.print_f64(%g)\n", d);
	  System.out.print("spectest.print_f64(");
	  System.out.print(d);
	  System.out.print(")\n");
	}

	static void spectest_print_f64_f64(double d1, double d2) {
	  //printf("spectest.print_f64_f64(%g %g)\n", d1, d2);
	  System.out.print("spectest.print_f64)f64(");
	  System.out.print(d1);
	  System.out.print(" ");
	  System.out.print(d2);
	  System.out.print(")\n");
	}

	static wasm_rt.Impl.Table spectest_table = new wasm_rt.Impl.Table(0, 0);
	static wasm_rt.Impl.Memory spectest_memory = new wasm_rt.Impl.Memory(0, 0);
	static int spectest_global_i32 = 666;

	//void (*Z_spectestZ_printZ_vv)(void) = &spectest_print;
	//void (*Z_spectestZ_print_i32Z_vi)(int) = &spectest_print_i32;
	//void (*Z_spectestZ_print_f32Z_vf)(float) = &spectest_print_f32;
	//void (*Z_spectestZ_print_i32_f32Z_vif)(int,
	//				       float) = &spectest_print_i32_f32;
	//void (*Z_spectestZ_print_f64Z_vd)(double) = &spectest_print_f64;
	//void (*Z_spectestZ_print_f64_f64Z_vdd)(double,
	//				       double) = &spectest_print_f64_f64;

	wasm_rt.Impl.ExternalRef<wasm_rt.Impl.Table> Z_spectestZ_table = new wasm_rt.Impl.ExternalRef<wasm_rt.Impl.Table> () {
		public wasm_rt.Impl.Table get() {
			return spectest_table;
		}
		public void set(wasm_rt.Impl.Table value) {
			spectest_table = value;
		}
	};
	wasm_rt.Impl.ExternalRef<wasm_rt.Impl.Memory> Z_spectestZ_memory = new wasm_rt.Impl.ExternalRef<wasm_rt.Impl.Memory> () {
		public wasm_rt.Impl.Memory get() {
			return spectest_memory;
		}
		public void set(wasm_rt.Impl.Memory value) {
			spectest_memory = value;
		}
	};
	wasm_rt.Impl.ExternalRefInt Z_spectestZ_global_i32Z_i = new wasm_rt.Impl.ExternalRefInt () {
		public int get() {
			return spectest_global_i32;
		}
		public void set(int value) {
			spectest_global_i32 = value;
		}
	};

	static void init_spectest_module() {
	  wasm_rt.Impl.allocate_memory(new wasm_rt.Impl.ExternalRef<wasm_rt.Impl.Memory> () {
		public wasm_rt.Impl.Memory get() {
			return spectest_memory;
		}
		public void set(wasm_rt.Impl.Memory value) {
			spectest_memory = value;
		}
	}, 1, 2);
	  wasm_rt.Impl.allocate_table(new wasm_rt.Impl.ExternalRef<wasm_rt.Impl.Table> () {
		public wasm_rt.Impl.Table get() {
			return spectest_table;
		}
		public void set(wasm_rt.Impl.Table value) {
			spectest_table = value;
		}
	}, 10, 20);
	}


	public static void main(String[] args) {
	  init_spectest_module();
	  run_spec_tests();
	  System.out.print(g_tests_passed);
	  System.out.print("/");
	  System.out.print(g_tests_run);
	  System.out.print(" tests passed.\n");
	  if (g_tests_passed != g_tests_run) {
		  System.exit(1);
	  } else {
		  System.exit(0);
	  }
	}
