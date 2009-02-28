#!/usr/bin/env clj
;;.hashdot.profile = shortlived
;;.test.prop = "hello world!"
;;

(let [msg (System/getProperty "test.prop")]
  (if msg
    (println msg)
    (throw (RuntimeException. "Property 'test.prop' not set"))))
