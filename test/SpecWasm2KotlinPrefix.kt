// these are written by the test runner
//@file:JvmName("SpecTestMain")
//package wabt.spec_test

var g_tests_run: Int = 0
var g_tests_passed: Int = 0

fun error(file: String, line: Int, format: String, args: Array<Any>) {
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
fun is_equal_u32(x: Int, y: Int): Boolean = x == y

fun is_equal_u64(x: Long, y: Long): Boolean = x == y

fun is_equal_f32(x: Float, y: Float): Boolean = x.toRawBits() == y.toRawBits()

fun is_equal_f64(x: Double, y: Double): Boolean = x.toRawBits() == y.toRawBits()

fun make_nan_f32(x: Int): Float = Float.fromBits(x or 0x7f800000)

fun make_nan_f64(x: Long): Double = Double.fromBits(x or 0x7ff0000000000000L)

fun is_canonical_nan_f32(x: Int): Boolean = (x and 0x7fffffff) == 0x7fc00000

fun is_canonical_nan_f64(x: Long): Boolean = (x and 0x7fffffffffffffffL) == 0x7ff8000000000000L

fun is_arithmetic_nan_f32(x: Int): Boolean = (x and 0x7fc00000) == 0x7fc00000

fun is_arithmetic_nan_f64(x: Long): Boolean = (x and 0x7ff8000000000000L) == 0x7ff8000000000000L


class Z_spectest() {
  
  /*
   * spectest implementations
   */
  fun spectest_print() {
    print("spectest.print()\n");
  }
  
  fun spectest_print_i32(i: Int) {
    print("spectest.print_i32(");
    print(i);
    print(")\n");
  }
  
  fun spectest_print_f32(f: Float) {
    //printf("spectest.print_f32(%g)\n", f);
    print("spectest.print_f32(");
    print(f);
    print(")\n");
  }
  
  fun spectest_print_i32_f32(i: Int, f: Float) {
    //printf("spectest.print_i32_f32(%d %g)\n", i, f);
    print("spectest.print_i32_f32(");
    print(i);
    print(" ");
    print(f);
    print(")\n");
  }
  
  fun spectest_print_f64(d: Double) {
    //printf("spectest.print_f64(%g)\n", d);
    print("spectest.print_f64(");
    print(d);
    print(")\n");
  }
  
  fun spectest_print_f64_f64(d1: Double, d2: Double) {
    //printf("spectest.print_f64_f64(%g %g)\n", d1, d2);
    print("spectest.print_f64)f64(");
    print(d1);
    print(" ");
    print(d2);
    print(")\n");
  }
  
  var spectest_table = wasm_rt_impl.Table(0, 0)
  var spectest_memory = wasm_rt_impl.Memory(0, 0)
  var spectest_global_i32: Int = 666
  
  //void (*Z_spectestZ_printZ_vv)(void) = &spectest_print;
  //void (*Z_spectestZ_print_i32Z_vi)(int) = &spectest_print_i32;
  //void (*Z_spectestZ_print_f32Z_vf)(float) = &spectest_print_f32;
  //void (*Z_spectestZ_print_i32_f32Z_vif)(int,
  //				       float) = &spectest_print_i32_f32;
  //void (*Z_spectestZ_print_f64Z_vd)(double) = &spectest_print_f64;
  //void (*Z_spectestZ_print_f64_f64Z_vdd)(double,
  //				       double) = &spectest_print_f64_f64;

  val Z_table = object : wasm_rt_impl.ExternalRef<wasm_rt_impl.Table> {
      override var value by this@Z_spectest::spectest_table
  }
  val Z_memory = object : wasm_rt_impl.ExternalRef<wasm_rt_impl.Memory> {
      override var value by this@Z_spectest::spectest_memory
  }
  val Z_global_i32Z_i = object : wasm_rt_impl.ExternalRef<Int> {
      override var value by this@Z_spectest::spectest_global_i32
  }

  init {
    wasm_rt_impl.allocate_memory(object : wasm_rt_impl.ExternalRef<wasm_rt_impl.Memory> {
        override var value by this@Z_spectest::spectest_memory
    }, 1, 2)
    wasm_rt_impl.allocate_table(object : wasm_rt_impl.ExternalRef<wasm_rt_impl.Table> {
        override var value by this@Z_spectest::spectest_table
    }, 10, 20)
  }
  
}

fun <T> run_test(fn: () -> T, mod: Z_spectest): T = fn()

fun <T> run_test(fn: (Z_spectest) -> T, mod: Z_spectest): T = fn(mod)

fun main(args: Array<String>) {
    val spectest = Z_spectest();
    run_spec_tests(spectest);

    print(g_tests_passed);
    print("/");
    print(g_tests_run);
    print(" tests passed.\n");

    if (g_tests_passed != g_tests_run) {
        System.exit(1);
    } else {
        System.exit(0);
    }
}
