; vim:syntax=scheme filetype=scheme expandtab
;;; This module runs a small http server.
;;; Most of it comes straight from Guile's manual.
;;; This module should be external.

(define-module (junkie www server))

(use-modules (junkie runtime) ; for set-thread-name and set-quit-when-done
             (junkie defs) ; for slog
             (web server)
             (web request)
             (web response)
             (web uri)
             (sxml simple)
             (sxml match)
             (srfi srfi-1) ; for fold
             (ice-9 regex)
             (ice-9 threads)
             (ice-9 match)
             (rnrs bytevectors)
             (rnrs io ports)
             (junkie instvars))

;;
;; Tools
;;

(define (print-headers request)
  `(table
     (tr (th "header") (th "value"))
     ,@(map (lambda (pair)
              `(tr (td (tt ,(with-output-to-string
                              (lambda () (display (car pair))))))
                   (td (tt ,(with-output-to-string
                              (lambda ()
                                (write (cdr pair))))))))
            (request-headers request))))

(define (print-params params)
  `(table
     (tr (th "name") (th "value"))
     ,@(map (lambda (pair)
              `(tr (td ,(car pair)) (td ,(cdr pair))))
            params)))

(define (uri-path-components uri)
  (split-and-decode-uri-path (uri-path uri)))

; returns an alist of (symbol . string-value)
; additionally, use the var name to choose a coercion?
(define (uri-query-components str)
  (let* ((pairs (string-split str #\&)))
    (map (lambda (pair)
           (let ((eq (string-index pair #\=)))
             (if eq
                 (cons (string->symbol (uri-decode (substring pair 0 eq)))
                       (uri-decode (substring pair (1+ eq))))
                 (cons (string->symbol (uri-decode pair)) 'unset))))
         pairs)))

(define (make-simple-page tree)
  `(html (body ,@tree)))

(define templatize make-simple-page)
(export templatize)

(define* (respond #:optional body #:key
                  (status 200)
                  (doctype "<!DOCTYPE html>\n")
                  (content-type-params '((charset . "utf-8")))
                  (content-type 'text/html)
                  (extra-headers '())
                  (sxml (and body (templatize body))))
         (list (build-response
                 #:code status
                 #:headers `((content-type
                               . (,content-type ,@content-type-params))
                             ,@extra-headers))
               (lambda (port)
                 (if sxml
                     (begin
                       (if doctype (display doctype port))
                       (sxml->xml sxml port))))))

(export respond)

(define* (respond-raw bv #:key
                      (status 200)
                      (content-type-params '((charset . "utf-8")))
                      (content-type 'text/html)
                      (extra-headers '()))
         (slog log-debug "respond-raw ~a bytes" (bytevector-length bv))
         (list (build-response
                 #:code status
                 #:headers `((content-type
                               . (,content-type ,@content-type-params))
                             ,@extra-headers))
               bv))

(export respond-raw)

;;
;; Pages
;;

(define (no-such-page path params)
  (slog log-debug "404 for path = ~s" path)
  (respond
    `((h1 "Is this what you were looking for?")
      ,(print-params params))
    #:status 404))

(define (error-page str)
  `((h1 "Error")
    (p ,str)))

(export error-page)

(define (content-type-of-filename fname)
  (let ((ext (string->symbol (car (reverse (string-split fname #\.))))))
    (slog log-debug "What's the content-type of a file which extension is ~s?" ext)
    (case ext
      ((ico)  'image/x-icon)
      ((css)  'text/css)
      ((pcap) 'application/vnd.tcpdump.pcap)
      (else   'text/plain))))

(define (respond-file file)
  (let ((input-port (open-file-input-port file))
        (quoted     (lambda (str)
                      (string-append "\"" str "\""))))
    (respond-raw (get-bytevector-all input-port)
                 #:content-type (content-type-of-filename file)
                 #:content-type-params '()
                 #:extra-headers `((content-disposition  . (attachment (filename . ,(quoted (basename file)))))))))

(export respond-file)

(define (static-dispatch path params)
  (match path
         ((path)
          (catch #t
                 (lambda ()
                   (let* ((full-path  (string-append wwwdir "/" path)))
                     (respond-file full-path)))
                 (lambda (key . args)
                   (slog log-err "Cannot serve ~s: ~s ~s" path key args)
                   #f)))
          (_ #f)))

(export static-dispatch)

;;
;; Server
;;

(define (run dispatch . rest)
  (let ((list->values (lambda (l) (apply values l))))
    (run-server (lambda (request body)
                  (let* ((uri     (request-uri request))
                         (method  (request-method request))
                         (path    (uri-path-components uri))
                         (body    (if body (utf8->string body) #f))
                         (ctype   (request-content-type request))
                         (query   (uri-query uri))
                         (gparams (if query (uri-query-components query) '()))
                         (pparams (if (and (eq? method 'POST)
                                           (eq? (car ctype) 'application/x-www-form-urlencoded)
                                           body)
                                      (uri-query-components
                                        (regexp-substitute/global #f "\\+" body 'pre " " 'post))
                                      '()))
                         (params  (append gparams pparams)))
                    (slog log-debug "method = ~s, path = ~s, body = ~s, params = ~s" method path body params)
                    (list->values (or (dispatch path params)
                                      (no-such-page path params)))))
                'http rest)))

(export run)

(define (chain-dispatchers l)
  (match l
         (()      (lambda (path params) #f))
         ((h . t) (lambda (path params)
                    (or (h path params)
                        ((chain-dispatchers t) path params))))))

(export chain-dispatchers)

;;
;; SXML transformers
;; TODO: test them!
;;

; Add a title to a document (adding the head if it's missing)
(define (add-title new-title tree)
  (sxml-match tree
              ; if there were already a title, change it
              [(html (head (title ,previous-title) ,h ...) ,body ...)
               `(html (head (title ,new-title) ,h ...) ,body ...)]
              ; if there were none, add one
              [(html (head ,h ...) ,body ...)
               `(html (head (title ,new-title) ,h ...) ,body ...)]
              ; if there were no head, add one
              [(html ,body ...)
               `(html (head (title ,new-title)) ,body ...)]))

(define (add-css href tree)
  (sxml-match tree
              [(html (head ,h ...) ,body ...)
               `(html (head ,h ... (link (@ (type . "text/css") (href . ,href) (rel . "stylesheet")) "")) ,body ...)]
              [(html ,body ...)
               `(html (head (link (@ (type . "text/css") (href . ,href) (rel . "stylesheet")) "")) ,body ...)]))

;;
;; Now what follows is specific to junkie's particular web server
;;

(define (add-header-footer tree)
  (sxml-match tree
              [(html ,pre-body ... (body ,body ...))
               `(html ,pre-body ... (body
                                      (div (@ (id "title")) (p "Junkie the Network Sniffer"))
                                      (div (@ (id "menu")))
                                      (div (@ (id "page")) ,body ...)
                                      (div (@ (id "footer")) (p ,junkie-version))))]))

; same pattern as above...
; Note: Inside an attr-list-pattern (@ ...) we cannot match for (a . b) but only (a b)
;       or compilation fails. So, we must not use the short version for div ids if we want to match them!
(define *current-path* (make-fluid))
(define (add-menu label href tree)
  (let* (; try to find the 'effective-path' of the current path (ie what page we are going to draw, ie what menu is active)
         (effective-path (match (fluid-ref *current-path*)
                                [("home")            "/home"]
                                [("crud" _ name . _) (string-append "/crud/read/" name)]
                                [l                   (string-append "/" (string-join l "/"))]))
         (selected       (string=? effective-path href)))
    (slog log-debug "Adding menu for href=~s, when effective-path=~s" href effective-path)
    (sxml-match tree
                [(html ,pre-body ... (body (div (@ (id "title")) ,title)
                                           (div (@ (id "menu")) ,m ...)
                                           ,post-menu ...))
                 `(html ,pre-body ... (body (div (@ (id "title")) ,title)
                                            (div (@ (id "menu"))
                                                 ,m ...
                                                 (a (@ (href . ,href)
                                                       (class . ,(if selected "selected" ""))) ; TODO
                                                    ,label))
                                            ,post-menu ...))])))

(define (homepage path params)
  (if (null? path)
      (respond
        `((h1 "It works! (or so, it seams)")
          (p "To learn more about junkie, see "
             (a (@ (href . "http://github.com/rixed/junkie")) here))))
      #f))


(define menus '(("Home" . "/")))

(define (add-menus label url)
  (set! menus (cons (cons label url) menus)))

(export add-menus)

(define dispatchers (list
                      homepage
                      static-dispatch))

(define (add-dispatcher f)
  (set! dispatchers (cons f dispatchers)))

(export add-dispatcher)

(define (start port)
  (set! templatize
    (fold (lambda (menu prev)
            (let ((name (car menu))
                  (url  (cdr menu)))
              (lambda (sxml)
                (add-menu name url (prev sxml)))))
          (lambda (sxml)
            (add-header-footer
              (add-css "/svg-graph.css"
                       (add-css "/junkie.css"
                                (add-title "Junkie"
                                           (make-simple-page sxml))))))
          (reverse menus)))
  (let ((dispatch (chain-dispatchers dispatchers)))
    (make-thread (lambda ()
                   (set-thread-name "J-www-server")
                   (run (lambda (path params)
                          (fluid-set! *current-path* path)
                          (dispatch path params))
                        #:port port)))))

(export start)

