; vim:syntax=scheme filetype=scheme expandtab
;;; This module defines some web pages to handle captures online

(define-module (junkie duplicogram))

(use-modules (ice-9 match)
             (ice-9 format)
             (srfi srfi-1)
             (junkie defs)
             (junkie runtime)
             (junkie www server))

(define (choose op a b)
  (if (or (not a)
          (op b a))
      b
      a))

(define (register)
  (load-plugin "duplicogram")
  (add-dispatcher
    (lambda (path params)
      (slog log-debug "Duplicodispatch for path ~s and params ~s" path params)
      (match path
             [("duplicogram" "home")
              (and=> (assq-ref params 'bucket-width)
                     (lambda (bw)
                       (set-duplicogram-bucket-width (string->number bw))))
              (let* ((width         1800)
                     (height        600)
                     (font-height   16)
                     (font-width    10)
                     (margin-bottom (- height (* 2 font-height)))
                     (tick-len      5)
                     (num-ticks-x    10)
                     (num-ticks-y    5)
                     (tick-every-x  #f) ; set later
                     (tick-every-y  #f)
                     (bucket-width  (get-duplicogram-bucket-width))
                     (max-dt        #f)
                     (max-dups      #f)
                     (min-dups      #f)
                     (min-dups-nz   #f) ; min for non-zero, positive values
                     (logarithmic   #f)
                     (pts           (get-duplicogram))
                     (add-space-y   0) ; will set later
                     (axis-x-y      margin-bottom) ; will set later to a better value, if possible (ie. there are dots)
                     (axis-y-x      (* 10 font-width)) ; TODO: dynamic X scale as well
                     (dups->lin-y   (lambda (dups min-dups max-dups)
                                      (let ((min-dups (if (and (> min-dups 0)
                                                               (< min-dups (/ max-dups 2)))
                                                          0 min-dups)))
                                        (- height add-space-y (/ (* (- dups min-dups) (- height add-space-y)) (- max-dups min-dups))))))
                     (dups->y       (lambda (dups)
                                      (if (= max-dups min-dups)
                                          axis-x-y
                                          (if logarithmic
                                              ; logarithmic scale
                                              (begin
                                                (let ((dups     (if (not (zero? dups))
                                                                    (log10 dups)
                                                                    (log10 min-dups-nz)))
                                                      (min-dups (if (not (zero? min-dups)) ; FIXME: precalc this min-dups for log scale
                                                                    (log10 min-dups)
                                                                    (log10 min-dups-nz)))
                                                      (max-dups (log10 max-dups))) ; we know from min-dups-nz that max-dups > 0
                                                  (dups->lin-y dups min-dups max-dups)))
                                              ; linear scale
                                              (dups->lin-y dups min-dups max-dups)))))
                     (dt->x         (lambda (dt)
                                      (if (> max-dt 0)
                                          (+ axis-y-x (/ (* dt (- width axis-y-x)) max-dt))
                                          0)))
                     (tick-interval (lambda (num-ticks range)
                                      (if (> range 0)
                                          (let ((expo (expt 10 (floor (log10 range)))))
                                            (* expo (/ (round (/ range expo)) num-ticks)))
                                          0))))
                (for-each (lambda (pt)
                            (let ((x (car pt))
                                  (y (cdr pt)))
                              (set! max-dt (choose > max-dt x))
                              (set! max-dups (choose > max-dups y))
                              (set! min-dups (choose < min-dups y))
                              (if (> y 0)
                                  (set! min-dups-nz (choose < min-dups-nz y)))))
                          pts)
                (and=> (assq-ref params 'max-dt)
                       (lambda (dt) (set! max-dt (string->number dt))))
                (and=> (assq-ref params 'logarithmic)
                       (lambda (b) (set! logarithmic b)))
                (set! add-space-y (let ((y0 (dups->y 0)))
                                    (if (> y0 margin-bottom)
                                        (- y0 margin-bottom)
                                        0)))
                (set! axis-x-y (dups->y 0))
                (set! tick-every-x (tick-interval num-ticks-x max-dt))
                (set! tick-every-y (tick-interval num-ticks-y max-dups))
                (slog log-debug "ticks: every ~s on X (0->~s) and every ~s on Y (~s->~s, min>0:~s)" tick-every-x max-dt tick-every-y min-dups max-dups min-dups-nz)
                (respond
                  `((form
                      (table
                        (tr (th "bucket width (usec):")
                            (td (input (@ (name . bucket-width)
                                          (value . ,bucket-width)))))
                        (tr (th "max DT (usec):")
                            (td (input (@ (name . max-dt)
                                          (value . ,max-dt)))))
                        (tr (th "logarithmic scale:")
                            (td (input (@ (type . checkbox)
                                          (name . logarithmic)
                                          ,@(if logarithmic '((checked . checked)) '())))))
                        (tr (th (@ (colspan . 2))
                                (input (@ (type . submit)
                                          (value . "change")))))))
                    ,(if (not min-dups-nz)
                         '(p "Not enough data")
                         `(svg (@ (class . "graph")
                                  (width . ,(string-append (number->string width) "px"))
                                  (height . ,(string-append (number->string height) "px")))
                               (descr ,(string-append "Plot for max " (number->string max-dups) " dups and dt up to " (number->string max-dt)
                                                      ", with bucket-width = " (number->string bucket-width)))
                               (marker (@ (id . "Arrowhead")
                                          (viewbox . "0 0 10 10")
                                          (refX . 0)
                                          (refY . 5)
                                          (markerUnits . "strokeWidth")
                                          (markerWidth . 4)
                                          (markerHeight . 3)
                                          (orient . "auto"))
                                       (path (@ (d . "M 0 0 L 10 5 L 0 10 z"))))
                               (g (@ (class . "axe axe-x"))
                                  (line (@ (x1 . ,(- axis-y-x 10))
                                           (y1 . ,axis-x-y)
                                           (x2 . ,(- width 10))
                                           (y2 . ,axis-x-y)
                                           (marker-end . "url(#Arrowhead)")))
                                  (text (@ (x . ,(- width 10))
                                           (y . ,(+ axis-x-y font-height))
                                           (font-size . ,font-width)
                                           (text-anchor . "end"))
                                        "DT (ms)"))
                               (g (@ (class . "axe axe-y"))
                                  (line (@ (x1 . ,axis-y-x)
                                           (y1 . ,(- height 10))
                                           (x2 . ,axis-y-x)
                                           (y2 . 10)
                                           (marker-end . "url(#Arrowhead)")))
                                  (text (@ (x . ,axis-y-x)
                                           (y . 10)
                                           (font-size . ,font-width)
                                           (text-anchor . "start"))
                                        "dups"))
                               ; ticks on Y
                               ,@(if (> tick-every-y 0)
                                     (unfold (lambda (d) (< (dups->y d) 10)) ; stop when Y < 10 (arrowhead approx size)
                                             (lambda (d)
                                               (let ((y (exact->inexact (dups->y d))))
                                                 `(g (@ (class . "tick tick-y"))
                                                     (line (@ (class . "grid")
                                                              (x1 . ,(exact->inexact axis-y-x))
                                                              (y1 . ,y)
                                                              (x2 . ,(exact->inexact width))
                                                              (y2 . ,y)))
                                                     (line (@ (x1 . ,(exact->inexact (- axis-y-x (/ tick-len 2))))
                                                              (y1 . ,y)
                                                              (x2 . ,(exact->inexact (+ axis-y-x (/ tick-len 2))))
                                                              (y2 . ,y)))
                                                     (text (@ (x . ,(exact->inexact (- axis-y-x (/ tick-len 2))))
                                                              (y . ,y)
                                                              (font-size . ,font-width)
                                                              (text-anchor . "end"))
                                                           ,(format #f "~,2g%" (* 100 d)))))) ; keep only a few digits in mantissa to avoid displaying rounding artefact
                                             (lambda (d) (+ d tick-every-y))
                                             0)
                                     '())
                               ; ticks on X
                               ,@(if (> tick-every-x 0)
                                     (unfold (lambda (t) (>= (dt->x t) (- width 10))) ; stop when Y < 10 (arrowhead approx size)
                                             (lambda (t)
                                               (let ((x (exact->inexact (dt->x t))))
                                                 `(g (@ (class . "tick tick-x"))
                                                     (line (@ (class . "grid")
                                                              (x1 . ,x)
                                                              (y1 . ,(exact->inexact axis-x-y))
                                                              (x2 . ,x)
                                                              (y2 . 0)))
                                                     (line (@ (x1 . ,x)
                                                              (y1 . ,(exact->inexact (- axis-x-y (/ tick-len 2))))
                                                              (x2 . ,x)
                                                              (y2 . ,(exact->inexact (+ axis-x-y (/ tick-len 2))))))
                                                     (text (@ (x . ,(exact->inexact (- x (/ font-width 2))))
                                                              (y . ,(exact->inexact (+ axis-x-y (/ (+ font-height tick-len) 2))))
                                                              (font-size . ,font-width)
                                                              (text-anchor . "start"))
                                                           ,(exact->inexact (/ t 1000))))))
                                             (lambda (t) (+ t tick-every-x))
                                             0)
                                     '())
                               (polyline (@ (class . "plot")
                                            (points . ,(fold (lambda (pt prev)
                                                               (if (<= (car pt) max-dt)
                                                                   (string-append (number->string (exact->inexact (dt->x (car pt)))) ","
                                                                                  (number->string (exact->inexact (dups->y (cdr pt)))) " " prev)
                                                                   prev))
                                                             ; FIXME: sort pts according to x
                                                             "" pts)))))))))]
             [_ #f])))
  (add-menus "duplicogram" "/duplicogram/home"))

(export register)

