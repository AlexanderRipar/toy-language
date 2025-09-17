Only files with certain prefixes are picked up by the test system. This is to
allow files that aren't themselves tests but that may be imported by tests.
The prefix must be of the form `(u|s)(f|t)_`. Additionally the file must end in
`.evl`.

When creating a new test you need to decide on the following:

- Should it be considered as a standard file? I.e. should builtins be allowed?
  If so, the first letter is `s`, for _standard_. Otherwise, it is `u` for
  _user_.

- Is it expected to succeed or to fail? \
  If the test should succeed, the second letter is `t` for _true_. Otherwise it
  is `f` for _false_.
