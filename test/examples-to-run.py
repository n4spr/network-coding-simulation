# A list of C++ examples to run in order to ensure that they remain
# buildable and runnable over time. Each tuple in the list contains
#
#     (example_name, do_run, do_valgrind_run, fullness).
#
# See test.py for more information.
cpp_examples = [
    ("network-coding-example", "True", "True", "QUICK"),
    ("network-coding-comparison-example", "True", "True", "EXTENSIVE"),
]

# No Python examples yet
python_examples = []
