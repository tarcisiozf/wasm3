(module
  ;; import function: env.foo(i32)
  (import "env" "foo" (func $foo (param i32)))

  ;; entry function
  (func (export "_start")
    i32.const 42
    call $foo
  )
)
