#!/usr/bin/env clj
;; Must symlink clj -> hashdot* in path since Clojure uses ;; comment style
;;.test.prop = "hello world!"
;;

(let [msg (System/getProperty "test.prop")]
  (if msg
    (println msg)
    (throw (RuntimeException. "Property 'test.prop' not set"))))