// these are written by the test runner
//@file:JvmName("SpecTestMain")
//package wabt.spec_test

import kotlin.text.StringBuilder
import java.nio.ByteBuffer
import java.math.BigInteger
import java.util.Arrays
import kotlin.reflect.full.primaryConstructor

var g_tests_run: Int = 0
var g_tests_passed: Int = 0

fun error(message: String, exception: Throwable) {
    System.err.println(message)
    exception.printStackTrace()
}

fun <T> ASSERT_TRAP(f: () -> T, name: BMap) {
    g_tests_run++
    try {
        f()
        error(StringBuilder("expected ").append(name.get("file")).append(":").append(name.get("line")).append(" to trap.").toString(), Exception())
    } catch (e: wasm_rt_impl.WasmException) {
        g_tests_passed++
    }
}

fun <T> ASSERT_EXHAUSTION(f: () -> T, name: BMap) {
    g_tests_run++
    try {
        f()
        error(StringBuilder("expected ").append(name.get("file")).append(":").append(name.get("line")).append(" to trap.").toString(), Exception())
    } catch (e: wasm_rt_impl.ExhaustionException) {
        g_tests_passed++
    } catch (e: wasm_rt_impl.WasmException) {
        error(StringBuilder("expected ").append(name.get("file")).append(":").append(name.get("line")).append(" to trap due to exhaustion.").toString(), e)
    }
}

fun FAIL(name: BMap) {
    g_tests_run++
        error(StringBuilder("UNIMPLEMENTED ").append(name.get("file")).append(":").append(name.get("line")).toString(), Exception())
}

fun <T> ASSERT_RETURN(f: () -> T, name: BMap) {
    g_tests_run++
    try {
        f()
        g_tests_passed++
    } catch (e: wasm_rt_impl.WasmException) {
        error(StringBuilder().append(name.get("file")).append(":").append(name.get("line")).append(" trapped.").toString(), e)
    }
}

inline fun <T> ASSERT_RETURN_T(f: () -> T, expected: T, is_equal: (T, T) -> Boolean, name: BMap) {
    g_tests_run++
    try {
        val value: T = f()
        if (is_equal(value, expected)) {
            g_tests_passed++
        } else {
            error(StringBuilder("in ").append(name.get("file")).append(":").append(name.get("line")).append(": expected ").append(expected).append(", got ").append(value).append(".").toString(), Exception())
        }
    } catch (e: wasm_rt_impl.WasmException) {
        error(StringBuilder().append(name.get("file")).append(":").append(name.get("line")).append(" trapped.").toString(), e)
    }
}

inline fun <T> ASSERT_RETURN_NAN_T(f: () -> T, is_nan_kind: (T) -> Boolean, name: BMap, nan: String) {
    g_tests_run++
    try {
        val value: T = f()
        if (is_nan_kind(value)) {
            g_tests_passed++
        } else {
            // FIXME(Soni): print the bits
            error(StringBuilder("in ").append(name.get("file")).append(":").append(name.get("line")).append(": expected ").append(nan).append(", got ").append(value).append(".").toString(), Exception())
        }
    } catch (e: wasm_rt_impl.WasmException) {
        error(StringBuilder().append(name.get("file")).append(":").append(name.get("line")).append(" trapped.").toString(), e)
    }
}


fun ASSERT_RETURN_I32(f: () -> Int, expected: Int, name: BMap) = ASSERT_RETURN_T(f, expected, ::is_equal_u32, name)
fun ASSERT_RETURN_I64(f: () -> Long, expected: Long, name: BMap) = ASSERT_RETURN_T(f, expected, ::is_equal_u64, name)
fun ASSERT_RETURN_F32(f: () -> Float, expected: Float, name: BMap) = ASSERT_RETURN_T(f, expected, ::is_equal_f32, name)
fun ASSERT_RETURN_F64(f: () -> Double, expected: Double, name: BMap) = ASSERT_RETURN_T(f, expected, ::is_equal_f64, name)

fun ASSERT_RETURN_CANONICAL_NAN_F32(f: () -> Float, name: BMap) = ASSERT_RETURN_NAN_T(f, ::is_canonical_nan_f32, name, "canonical")
fun ASSERT_RETURN_CANONICAL_NAN_F64(f: () -> Double, name: BMap) = ASSERT_RETURN_NAN_T(f, ::is_canonical_nan_f64, name, "canonical")
fun ASSERT_RETURN_ARITHMETIC_NAN_F32(f: () -> Float, name: BMap) = ASSERT_RETURN_NAN_T(f, ::is_arithmetic_nan_f32, name, "arithmetic")
fun ASSERT_RETURN_ARITHMETIC_NAN_F64(f: () -> Double, name: BMap) = ASSERT_RETURN_NAN_T(f, ::is_arithmetic_nan_f64, name, "arithmetic")

fun is_equal_u32(x: Int, y: Int): Boolean = x == y
fun is_equal_u64(x: Long, y: Long): Boolean = x == y
fun is_equal_f32(x: Float, y: Float): Boolean = x.toRawBits() == y.toRawBits()
fun is_equal_f64(x: Double, y: Double): Boolean = x.toRawBits() == y.toRawBits()

fun make_nan_f32(x: Int): Float = Float.fromBits(x or 0x7f800000)
fun make_nan_f64(x: Long): Double = Double.fromBits(x or 0x7ff0000000000000L)

fun is_canonical_nan_f32(x: Float):   Boolean = (x.toRawBits() and 0x7fffffff) == 0x7fc00000
fun is_arithmetic_nan_f32(x: Float):  Boolean = (x.toRawBits() and 0x7fc00000) == 0x7fc00000
fun is_canonical_nan_f64(x: Double):  Boolean = (x.toRawBits() and 0x7fffffffffffffffL) == 0x7ff8000000000000L
fun is_arithmetic_nan_f64(x: Double): Boolean = (x.toRawBits() and 0x7ff8000000000000L) == 0x7ff8000000000000L


class Z_spectest(moduleRegistry: wasm_rt_impl.ModuleRegistry, name: String) {
  
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
    print("spectest.print_f32(");
    print(f);
    print(")\n");
  }
  
  fun spectest_print_i32_f32(i: Int, f: Float) {
    print("spectest.print_i32_f32(");
    print(i);
    print(" ");
    print(f);
    print(")\n");
  }
  
  fun spectest_print_f64(d: Double) {
    print("spectest.print_f64(");
    print(d);
    print(")\n");
  }
  
  fun spectest_print_f64_f64(d1: Double, d2: Double) {
    print("spectest.print_f64_f64(");
    print(d1);
    print(" ");
    print(d2);
    print(")\n");
  }
  
  var spectest_table: wasm_rt_impl.Table = wasm_rt_impl.Table(0, 0)
  var spectest_memory: wasm_rt_impl.Memory = wasm_rt_impl.Memory(0, 0)
  var spectest_global_i32: Int = 666
  var spectest_global_i64: Long = 666
  var spectest_global_f32: Float = 666f
  var spectest_global_f64: Double = 666.0
  
  init {
      moduleRegistry.exportFunc(name, "Z_printZ_vv", this@Z_spectest::spectest_print);
      moduleRegistry.exportFunc(name, "Z_print_i32Z_vi", this@Z_spectest::spectest_print_i32);
      moduleRegistry.exportFunc(name, "Z_print_f32Z_vf", this@Z_spectest::spectest_print_f32);
      moduleRegistry.exportFunc(name, "Z_print_i32_f32Z_vif", this@Z_spectest::spectest_print_i32_f32);
      moduleRegistry.exportFunc(name, "Z_print_f64Z_vd", this@Z_spectest::spectest_print_f64);
      moduleRegistry.exportFunc(name, "Z_print_f64_f64Z_vdd", this@Z_spectest::spectest_print_f64_f64);

      moduleRegistry.exportTable(name, "Z_table", this@Z_spectest::spectest_table);
      moduleRegistry.exportMemory(name, "Z_memory", this@Z_spectest::spectest_memory);

      moduleRegistry.exportConstant(name, "Z_global_i32Z_i", this@Z_spectest::spectest_global_i32);
      moduleRegistry.exportConstant(name, "Z_global_i64Z_j", this@Z_spectest::spectest_global_i64);
      moduleRegistry.exportConstant(name, "Z_global_f32Z_f", this@Z_spectest::spectest_global_f32);
      moduleRegistry.exportConstant(name, "Z_global_f64Z_d", this@Z_spectest::spectest_global_f64);
  }

  init {
    wasm_rt_impl.allocate_memory(this@Z_spectest::spectest_memory, 1, 2)
    wasm_rt_impl.allocate_table(this@Z_spectest::spectest_table, 10, 20)
  }
  
}

fun <T> getGlobal(moduleRegistry: wasm_rt_impl.ModuleRegistry, modname: String, fieldname: String): T {
    try {
        return moduleRegistry.importGlobal<T>(modname, fieldname).get()
    } catch (e: NullPointerException) {
        return moduleRegistry.importConstant<T>(modname, fieldname).get()
    }
}

object End;

data class Bytes(val bytes: ByteArray) {
    override fun hashCode(): Int {
        return Arrays.hashCode(bytes)
    }

    override fun equals(other: Any?): Boolean {
        if (this === other) { return true }
        if (other === null) { return false }
        if (other !is Bytes) { return false }
        return Arrays.equals(bytes, other.bytes)
    }

    override fun toString(): String {
        return this.bytes.toString(Charsets.UTF_8)
    }
}

data class BList(val list: ArrayList<Any>);
data class BMap(val map: HashMap<Bytes, Any>) {
    fun get(s: String): Any? = this.map.get(Bytes(s.toByteArray()))
}

fun parseInput(s: ByteBuffer, end: Boolean = false): Any? {
    // modified bencode
    fun readNum(s: ByteBuffer): BigInteger {
        val sb = StringBuilder()
        if (s.hasRemaining()) {
            s.mark()
            val first = s.get().toInt()
            if (first == 0x2D) {
                sb.append("-")
            } else {
                s.reset()
            }
        }
        while (s.hasRemaining()) {
            s.mark()
            val key = s.get()
            if (key.toInt() in 0x30..0x39) {
                sb.append(key.toInt().toChar())
            } else {
                s.reset()
                break
            }
        }
        return BigInteger(sb.toString())
    }

    if (!s.hasRemaining()) {
        return null
    }
    s.mark()
    val result: Any = when (s.get().toInt()) {
        0x65 -> End // lowercase 'e' -> End
        0x69 -> { // lowercase 'i' -> number
            val num = readNum(s)
            if (!s.hasRemaining() || s.get().toInt() != 0x65) {
                throw RuntimeException("malformed number")
            }
            num
        }
        in 0x30..0x39 -> { // '0' to '9' inclusive -> bytes
            s.reset()
            val len = readNum(s)
            if (len.compareTo(BigInteger.valueOf(0x7FFFFFFFL)) > 0) {
                throw RuntimeException("malformed length")
            }
            if (!s.hasRemaining() || s.get().toInt() != 0x3A || s.remaining() < len.toInt()) {
                throw RuntimeException("malformed bytes")
            }
            val bytes = ByteArray(len.toInt())
            s.get(bytes)
            Bytes(bytes)
        }
        0x6C -> { // lowercase 'l'
            val list = ArrayList<Any>()
            while (true) {
                when (val value = parseInput(s)) {
                    null -> throw RuntimeException("malformed list")
                    is End -> break
                    else -> list.add(value)
                }
            }
            BList(list)
        }
        0x64 -> { // lowercase 'd'
            val map = HashMap<Bytes, Any>()
            while (true) {
                map.put(when (val key = parseInput(s)) {
                    is End -> break
                    !is Bytes -> throw RuntimeException("malformed dict")
                    else -> key
                }, when (val value = parseInput(s)) {
                    null -> throw RuntimeException("malformed dict")
                    is End -> throw RuntimeException("malformed dict")
                    else -> value
                })
            }
            BMap(map)
        }
        else -> throw RuntimeException("malformed bencode")
    }
    if (end && s.hasRemaining()) {
        throw RuntimeException()
    }
    return result
}

fun runString(moduleRegistry: wasm_rt_impl.ModuleRegistry, s: String) {
    val input = parseInput(ByteBuffer.wrap(s.toByteArray()).asReadOnlyBuffer(), true)
    val runner = Runner(moduleRegistry)
    if (input is BList) {
        for (command in input.list) {
            if (command is BMap) {
                runner.runCommand(command)
            }
        }
    }
}

class Runner(val moduleRegistry: wasm_rt_impl.ModuleRegistry) {

    fun runCommand(command: BMap) {
        val type = command.get("type")
        if (type !is Bytes) return
        when (type.toString()) {
            "module" -> runModuleCommand(command)
            "assert_uninstantiable" -> runAssertUninstantiableCommand(command)
            "action" -> runActionCommand(command)
            "assert_return" -> runAssertReturnCommand(command)
            "assert_trap" -> runAssertActionCommand(command)
            "assert_exhaustion" -> runAssertActionCommand(command)
            else -> {}
        }
    }

    fun runModuleCommand(command: BMap) {
        val prefix = command.get("prefix")
        if (prefix !is Bytes) return
        val sprefix = prefix.toString()
        val cls = Class.forName("wabt.spec_test." + sprefix).kotlin
        try {
            cls.primaryConstructor!!.call(this.moduleRegistry, sprefix)
        } catch (e: java.lang.reflect.InvocationTargetException) {
            // unwrap exception
            throw e.getTargetException()
        }
    }

    fun runAssertUninstantiableCommand(command: BMap) {
        ASSERT_TRAP({ runModuleCommand(command) }, command)
    }

    fun runActionCommand(command: BMap) {
        action(command)
    }

    fun runAssertReturnCommand(command: BMap) {
        val expected = command.get("expected") as BList
        if (expected.list.size == 1) {
            val type = ((expected.list.get(0) as BMap).get("type") as Bytes).toString()
            val value = ((expected.list.get(0) as BMap).get("value") as Bytes).toString()
            if (value == "nan:canonical") {
                when (type) {
                    "f32" -> ASSERT_RETURN_CANONICAL_NAN_F32({ action(command) as Float }, command)
                    "f64" -> ASSERT_RETURN_CANONICAL_NAN_F64({ action(command) as Double }, command)
                    else -> throw RuntimeException()
                }
            } else if (value == "nan:arithmetic") {
                when (type) {
                    "f32" -> ASSERT_RETURN_ARITHMETIC_NAN_F32({ action(command) as Float }, command)
                    "f64" -> ASSERT_RETURN_ARITHMETIC_NAN_F64({ action(command) as Double }, command)
                    else -> throw RuntimeException()
                }
            } else {
                @Suppress("NAME_SHADOWING")
                val value = BigInteger(value)
                when (type) {
                    "i32" -> ASSERT_RETURN_I32({ action(command) as Int }, value.toInt(), command)
                    "f32" -> ASSERT_RETURN_F32({ action(command) as Float }, Float.fromBits(value.toInt()), command)
                    "i64" -> ASSERT_RETURN_I64({ action(command) as Long }, value.toLong(), command)
                    "f64" -> ASSERT_RETURN_F64({ action(command) as Double }, Double.fromBits(value.toLong()), command)
                    else -> throw RuntimeException()
                }
            }
        } else if (expected.list.size == 0) {
            runAssertActionCommand(command)
        } else {
            FAIL(command)
        }
    }

    fun runAssertActionCommand(command: BMap) {
        val action = (command.get("type") as Bytes).toString()
        when (action) {
            "assert_exhaustion" -> ASSERT_EXHAUSTION({
                action(command)
            }, command)
            "assert_return" -> ASSERT_RETURN({
                action(command)
            }, command)
            "assert_trap" -> ASSERT_TRAP({
                action(command)
            }, command)
        }
    }

    fun action(command: BMap): Any {
        val action = command.get("action") as BMap
        val type = (action.get("type") as Bytes).toString()
        val module = (command.get("mangled_module_name") as Bytes).toString()
        val field = (command.get("field") as Bytes).toString()
        when (type) {
            "invoke" -> {
                val func = moduleRegistry.importFunc<Function<Any>, Any>(module, field)
                val args = when (val action_args = action.get("args")) {
                    null -> Array<Any?>(0) { null }
                    is BList -> Array<Any?>(action_args.list.size) {
                        val arg = action_args.list.get(it) as BMap
                        val valtype = (arg.get("type") as Bytes).toString()
                        val value = BigInteger((arg.get("value") as Bytes).toString())
                        when (valtype) {
                            "f32" -> Float.fromBits(value.toInt())
                            "f64" -> Double.fromBits(value.toLong())
                            "i32" -> value.toInt()
                            "i64" -> value.toLong()
                            else -> throw RuntimeException()
                        }
                    }
                    else -> throw RuntimeException()
                }
                val cls = func::class.java
                val invoke = cls.getDeclaredMethod("invoke", *Array(args.size) { java.lang.Object::class.java })
                try {
                    return invoke.invoke(func, *args) ?: Unit
                } catch (e: java.lang.reflect.InvocationTargetException) {
                    // unwrap exception
                    throw e.getTargetException()
                }
            }
            "get" -> return getGlobal(moduleRegistry, module, field)
            else -> throw RuntimeException("Unexpected action type " + type)
        }
    }
}

fun main() {
    val moduleRegistry = wasm_rt_impl.ModuleRegistry()
    Z_spectest(moduleRegistry, "Z_spectest");
    run_spec_tests(moduleRegistry);

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
