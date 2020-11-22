// these are written by the test runner
//@file:JvmName("SpecTestMain")
//package wabt.spec_test

var g_tests_run: Int = 0
var g_tests_passed: Int = 0

fun error(message: String, exception: Throwable) {
    System.err.println(message)
    exception.printStackTrace()
}

fun <T> ASSERT_TRAP(f: () -> T) {
    g_tests_run++
    try {
        f()
        error("expected f to trap.", Exception())
    } catch (e: wasm_rt_impl.WasmException) {
        g_tests_passed++
    }
}

fun <T> ASSERT_EXHAUSTION(f: () -> T) {
    g_tests_run++
    try {
        f()
        error("expected f to trap.", Exception())
    } catch (e: wasm_rt_impl.ExhaustionException) {
        g_tests_passed++
    } catch (e: wasm_rt_impl.WasmException) {
        error("expected f to trap due to exhaustion.", e)
    }
}

fun <T> ASSERT_RETURN(f: () -> T) {
    g_tests_run++
    try {
        f()
        g_tests_passed++
    } catch (e: wasm_rt_impl.WasmException) {
        error("f trapped.", e)
    }
}

fun <T> ASSERT_RETURN_T(f: () -> T, expected: T, is_equal: (T, T) -> Boolean) {
    g_tests_run++
    try {
        val value: T = f()
        if (is_equal(value, expected)) {
            g_tests_passed++
        } else {
            error("in f: expected , got .", Exception())
        }
    } catch (e: wasm_rt_impl.WasmException) {
        error("f trapped.", e)
    }
}

fun <T> ASSERT_RETURN_NAN_T(f: () -> T, is_nan_kind: (T) -> Boolean) {
    g_tests_run++
    try {
        val value: T = f()
        if (is_nan_kind(value)) {
            g_tests_passed++
        } else {
            error("in f: expected , got .", Exception())
        }
    } catch (e: wasm_rt_impl.WasmException) {
        error("f trapped.", e)
    }
}


fun ASSERT_RETURN_I32(f: () -> Int, expected: Int) = ASSERT_RETURN_T(f, expected, ::is_equal_u32)
fun ASSERT_RETURN_I64(f: () -> Long, expected: Long) = ASSERT_RETURN_T(f, expected, ::is_equal_u64)
fun ASSERT_RETURN_F32(f: () -> Float, expected: Float) = ASSERT_RETURN_T(f, expected, ::is_equal_f32)
fun ASSERT_RETURN_F64(f: () -> Double, expected: Double) = ASSERT_RETURN_T(f, expected, ::is_equal_f64)

fun ASSERT_RETURN_CANONICAL_NAN_F32(f: () -> Float) = ASSERT_RETURN_NAN_T(f, ::is_canonical_nan_f32)
fun ASSERT_RETURN_CANONICAL_NAN_F64(f: () -> Double) = ASSERT_RETURN_NAN_T(f, ::is_canonical_nan_f64)
fun ASSERT_RETURN_ARITHMETIC_NAN_F32(f: () -> Float) = ASSERT_RETURN_NAN_T(f, ::is_arithmetic_nan_f32)
fun ASSERT_RETURN_ARITHMETIC_NAN_F64(f: () -> Double) = ASSERT_RETURN_NAN_T(f, ::is_arithmetic_nan_f64)

fun is_equal_u32(x: Int, y: Int): Boolean = x == y
fun is_equal_u64(x: Long, y: Long): Boolean = x == y
fun is_equal_f32(x: Float, y: Float): Boolean = x.toRawBits() == y.toRawBits()
fun is_equal_f64(x: Double, y: Double): Boolean = x.toRawBits() == y.toRawBits()

fun make_nan_f32(x: Int): Float = Float.fromBits(x or 0x7f800000)
fun make_nan_f64(x: Long): Double = Double.fromBits(x or 0x7ff0000000000000L)


fun is_canonical_nan_f32(x: Float): Boolean = (x.toRawBits() and 0x7fffffff) == 0x7fc00000
fun is_canonical_nan_f64(x: Double): Boolean = (x.toRawBits() and 0x7fffffffffffffffL) == 0x7ff8000000000000L
fun is_arithmetic_nan_f32(x: Float): Boolean = (x.toRawBits() and 0x7fc00000) == 0x7fc00000
fun is_arithmetic_nan_f64(x: Double): Boolean = (x.toRawBits() and 0x7ff8000000000000L) == 0x7ff8000000000000L


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

  var Z_table = this@Z_spectest::spectest_table
  var Z_memory = this@Z_spectest::spectest_memory
  var Z_global_i32Z_i = this@Z_spectest::spectest_global_i32

  init {
    wasm_rt_impl.allocate_memory(this@Z_spectest::spectest_memory, 1, 2)
    wasm_rt_impl.allocate_table(this@Z_spectest::spectest_table, 10, 20)
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
