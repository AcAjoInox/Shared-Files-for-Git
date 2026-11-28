# msgcheck.awk - controlla gli argomenti dei testi della tabella dei
# messaggi (C-16 di report/bug-aperti.md).
#
#   awk -f msgcheck.awk src/msg.c src/*.c utils/*.c
#
# Il compilatore non controlla printf(msg(MSG_X), ...): il formato non e'
# un letterale (-Wformat-nonliteral). Qui, per ogni chiamata con un testo
# della tabella come formato, si contano le conversioni del testo inglese
# (%s, %u, %lu, %-6s, ... ; %% non conta, * conta come un argomento) e gli
# argomenti dopo il formato:
#   msg_error / msg_warning / msg_fatal (MSG_X, ...)
#   printf(msg(MSG_X), ...)   fprintf(f, msg(MSG_X), ...)
#   str_addf(s, msg(MSG_X), ...)
#   fputs(msg(MSG_X), f)       (nessuna conversione: il testo e' stampato
#                               cosi' com'e')
# Un formato con piu' ID (cond ? MSG_A : MSG_B) si controlla per ognuno.
# Ogni conversione deve essere una di quelle che il codice usa: s, u, d, c,
# x, lu, ld, zu (con flag e larghezza).
# Stampa gli errori e, alla fine, "checked <n> calls, <m> errors".

BEGIN {
    nerr = 0
    ncalls = 0
}

# Il file intero in una stringa.
FNR == 1 {
    if (file != "") {
        process(file, text)
    }
    file = FILENAME
    text = ""
}
{
    text = text $0 "\n"
}
END {
    if (file != "") {
        process(file, text)
    }
    printf "checked %d calls, %d errors\n", ncalls, nerr
    exit (nerr > 0) ? 1 : 0
}

function process(f, t) {
    t = strip_comments(t)
    if (f ~ /msg\.c$/) {
        read_table(t)
    } else {
        scan_calls(f, t)
    }
}

# Toglie i commenti /* */ e //, lasciando stringhe e caratteri.
function strip_comments(t,    out, i, n, c, d, q) {
    out = ""
    n = length(t)
    i = 1
    while (i <= n) {
        c = substr(t, i, 1)
        d = substr(t, i, 2)
        if (d == "/*") {
            q = index(substr(t, i + 2), "*/")
            c = substr(t, i, (q > 0) ? (q + 3) : (n - i + 1))
            i = (q > 0) ? (i + 2 + q + 1) : (n + 1)
            out = out " " newlines(c) # le righe restano al loro posto
        } else if (d == "//") {
            while ((i <= n) && (substr(t, i, 1) != "\n")) {
                i++
            }
        } else if ((c == "\"") || (c == "'")) {
            q = skip_literal(t, i)
            out = out substr(t, i, q - i)
            i = q
        } else {
            out = out c
            i++
        }
    }
    return out
}

# Solo gli a capo di s.
function newlines(s,    out, k) {
    out = ""
    for (k = gsub(/\n/, "\n", s); k > 0; k--) {
        out = out "\n"
    }
    return out
}

# Posizione dopo il letterale (stringa o carattere) che comincia in i.
function skip_literal(t, i,    c, q, n) {
    q = substr(t, i, 1)
    n = length(t)
    i++
    while (i <= n) {
        c = substr(t, i, 1)
        if (c == "\\") {
            i += 2
        } else if (c == q) {
            return i + 1
        } else {
            i++
        }
    }
    return i
}

# Tabella msg_en: [MSG_X] = "..." "..." ,
function read_table(t,    start, i, n, id, j, s, c, q) {
    start = index(t, "msg_en[] = {")
    if (start == 0) {
        print "msgcheck: table msg_en not found"
        nerr++
        return
    }
    n = length(t)
    i = start
    while (i <= n) {
        if (substr(t, i, 2) == "};") {
            break
        }
        if (substr(t, i, 5) == "[MSG_") {
            j = index(substr(t, i), "]")
            id = substr(t, i + 1, j - 2)
            i += j
            s = ""
            while (i <= n) {
                c = substr(t, i, 1)
                if (c == "\"") {
                    q = skip_literal(t, i)
                    s = s substr(t, i + 1, q - i - 2)
                    i = q
                } else if ((c == ",") || (c == "}")) {
                    break
                } else {
                    i++
                }
            }
            fmt[id] = s
            nconv[id] = count_conv(id, s)
        } else {
            i++
        }
    }
}

# Conversioni di un testo; un'conversione sconosciuta e' un errore.
function count_conv(id, s,    i, n, c, k, spec) {
    k = 0
    n = length(s)
    i = 1
    while (i <= n) {
        c = substr(s, i, 1)
        if (c != "%") {
            i++
            continue
        }
        if (substr(s, i + 1, 1) == "%") {
            i += 2
            continue
        }
        i++
        while (index("-+ #0", substr(s, i, 1)) > 0) {
            i++
        }
        while (index("0123456789*", substr(s, i, 1)) > 0) {
            if (substr(s, i, 1) == "*") {
                k++
            }
            i++
        }
        if (substr(s, i, 1) == ".") {
            i++
            while (index("0123456789*", substr(s, i, 1)) > 0) {
                if (substr(s, i, 1) == "*") {
                    k++
                }
                i++
            }
        }
        spec = ""
        while (index("hlzjt", substr(s, i, 1)) > 0) {
            spec = spec substr(s, i, 1)
            i++
        }
        spec = spec substr(s, i, 1)
        i++
        if ((spec != "s") && (spec != "u") && (spec != "d") && (spec != "c") &&
            (spec != "x") && (spec != "lu") && (spec != "ld") && (spec != "zu")) {
            printf "msg.c: %s: unknown conversion %%%s\n", id, spec
            nerr++
        }
        k++
    }
    return k
}

# Argomenti della chiamata la cui '(' e' in t[p]: nargs, arg[1..nargs].
# Ritorna la posizione dopo la ')'.
function parse_args(t, p,    n, depth, i, c, cur) {
    n = length(t)
    depth = 0
    nargs = 0
    cur = ""
    delete arg
    i = p + 1
    while (i <= n) {
        c = substr(t, i, 1)
        if ((c == "\"") || (c == "'")) {
            q = skip_literal(t, i)
            cur = cur substr(t, i, q - i)
            i = q
            continue
        }
        if ((c == "(") || (c == "[") || (c == "{")) {
            depth++
        } else if (((c == ")") || (c == "]") || (c == "}")) && (depth > 0)) {
            depth--
        } else if ((c == ")") && (depth == 0)) {
            if ((nargs > 0) || (cur ~ /[^ \t\n]/)) {
                arg[++nargs] = cur
            }
            return i + 1
        } else if ((c == ",") && (depth == 0)) {
            arg[++nargs] = cur
            cur = ""
            i++
            continue
        }
        cur = cur c
        i++
    }
    return i
}

# Controlla gli ID di testo che compaiono in idtext con `given` argomenti.
# Meno argomenti delle conversioni: comportamento indefinito. Di piu': gli
# argomenti in piu' sono ignorati (C11 7.21.6.1), quindi vanno bene con piu'
# ID (cond ? MSG_A : MSG_B, testi diversi), non con uno solo.
function check(f, line, idtext, given, how,    s, m, id, many) {
    s = idtext
    many = gsub(/MSG_[A-Z0-9_]+/, "&", s) > 1
    while (match(s, /MSG_[A-Z0-9_]+/)) {
        id = substr(s, RSTART, RLENGTH)
        s = substr(s, RSTART + RLENGTH)
        if (!(id in fmt)) {
            printf "%s:%d: %s: not in the table\n", f, line, id
            nerr++
            continue
        }
        ncalls++
        if ((nconv[id] > given) || ((nconv[id] < given) && !many)) {
            printf "%s:%d: %s %s: the text wants %d arguments, the call gives %d\n",
                f, line, how, id, nconv[id], given
            nerr++
        }
    }
}

function line_of(t, p,    head) {
    head = substr(t, 1, p)
    return gsub(/\n/, "\n", head) + 1
}

function scan_calls(f, t,    n, i, rest, name, p, end, fi, a, k) {
    n = length(t)
    i = 1
    while (i <= n) {
        rest = substr(t, i)
        if (!match(rest, /(msg_error|msg_warning|msg_fatal|printf|fprintf|str_addf|fputs)[ \t]*\(/)) {
            break
        }
        name = substr(rest, RSTART, RLENGTH)
        sub(/[ \t]*\($/, "", name)
        p = i + RSTART + RLENGTH - 2 # la '('
        # parola intera: "vprintf", "my_printf" non contano
        if ((i + RSTART - 2 >= 1) && (substr(t, i + RSTART - 2, 1) ~ /[A-Za-z0-9_]/)) {
            i = p + 1
            continue
        }
        end = parse_args(t, p)
        if ((name == "msg_error") || (name == "msg_warning") || (name == "msg_fatal")) {
            if ((nargs >= 1) && (arg[1] ~ /MSG_/)) {
                check(f, line_of(t, p), arg[1], nargs - 1, name)
            }
        } else {
            fi = ((name == "fprintf") || (name == "str_addf")) ? 2 : 1
            if ((nargs >= fi) && (arg[fi] ~ /^[ \t\n]*msg[ \t]*\(/)) {
                if (name == "fputs") {
                    check(f, line_of(t, p), arg[fi], 0, name)
                } else {
                    check(f, line_of(t, p), arg[fi], nargs - fi, name)
                }
            }
        }
        i = p + 1 # anche le chiamate annidate
    }
}
