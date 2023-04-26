private class Delegate(
    var level: Int,
    val ex: Exception,
): Throwable(null, null, false, false);
