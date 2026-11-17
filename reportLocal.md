# Report sull'uso locale — git shared

> **Aggiornato al 2026-11-17.** Baseline `project.md` v2.29 (D-001…D-091). Codice v0 con unione delle divergenze, ri-aggancio, comandi di manutenzione completi, `status --peers`, manuale per comando in inglese e italiano, catena letta come grafo: 845 test su 845 su Windows (git 2.43) e su Linux in WSL2 (git 2.34 e 2.56.1). Riguarda solo la fase 1 (repo sulla stessa macchina). I bug sono in `reportIdea.md` (file locale, non versionato); qui ci sono due giudizi: l'impatto atteso e la facilità d'uso.
>
> **Da dove vengono i giudizi.** Dalla lettura del codice, dai test e dalle prove fatte a mano con l'eseguibile durante la ricerca dei bug. Nessuno, a parte l'autore, lo ha ancora usato: la parte sull'uso è una valutazione ragionata, non una misura.
>
> **Storia.** 2026-11-16: prima stesura (718 test, v2.24). 2026-11-17: Linux provato e compatibilità dichiarata chiusa (C-25, D-089, D-090; non provata solo git 2.34 su Windows, ritenuta stabile); seconda ricerca di bug sul flusso logico e sulla struttura (§9: B-46…B-51, C-26, riportati anche in `reportIdea.md`); B-46, l'unico grave, chiuso (D-091); tutto il report riallineato a questo stato.

## 1. In breve

- **L'impatto atteso sulla community non è cambiato: resta di nicchia.** Il motivo è lo stesso di prima: la fase 1 funziona solo tra repo della stessa macchina, quindi non si può condividere con un collega.
- **È cambiato il valore per chi lo usa da solo.** Prima del merge una divergenza era un vicolo cieco; ora ogni stato ha un comando che ne fa uscire. Da "prototipo che funziona nei casi buoni" è diventato "strumento usabile ogni giorno da chi lo conosce".
- **È cambiata la credibilità come dimostrazione.** La promessa che lo distingue dagli altri strumenti ("nessuna modifica si perde, le divergenze si uniscono", `project.md` §2.3 punto 5) ora è codice provato, non un requisito.
- **L'uso quotidiano è semplice**: cinque comandi, uno o due in più rispetto al normale lavoro con git. **L'uso fuori dal caso normale è meno semplice**: dieci stati, e alcune trappole in cui si finisce senza aver sbagliato nulla (§4).
- **Le trappole più probabili sono chiuse** (B-39, B-40, C-20, clone: D-079, D-080; manutenzione: D-081…D-085). Prima di usarlo sul lavoro vero restano da sapere quelle di §4 ancora aperte, quasi tutte nelle unioni tra versioni (B-47, B-48).
- **Nessun bug grave aperto.** B-46, trovato dalla seconda ricerca (§9), è chiuso (D-091): la versione di un peer unita da un'altra repo ora è riconosciuta, e il suo `update` non rimette più ciò che l'altra repo aveva tolto.

## 2. È migliorata l'aspettativa di impatto?

| Aspetto | Prima del merge | Adesso | Cambia? |
|---|---|---|---|
| Uso personale, più repo sulla stessa macchina | Funziona finché nessuno modifica da una versione vecchia; poi "merging is not implemented yet" | Ciclo completo: modifica, pubblicazione, applicazione, unione, conflitto | **Sì, molto** |
| Robustezza | 1 dato salvato fragile previsto (merge in `.git/`), vicoli ciechi con squash e amend | Stato sempre ricavato; l'unica ref salvata vale solo se l'index la conferma; squash e amend si chiudono con un `update` | **Sì** |
| Valore come dimostrazione ("un file condiviso con una ref e il solo trasporto git") | Idea documentata, metà realizzata | Realizzata per il locale, con 845 test e un registro di 91 decisioni | **Sì** |
| Adozione da parte di altri | Di nicchia | Di nicchia | **No** |
| Uso in un gruppo | Impossibile (una macchina) | Impossibile (una macchina) | **No** |

**Perché l'adozione non cambia.**
- Chi cerca "condividere un file tra repo" quasi sempre ha un collega o una CI dall'altra parte. La fase 1 non li raggiunge.
- Ogni clone va ricollegato a mano con un `link` (ora funziona, ma va saputo): niente segue la repo da sola.
- Va installato a mano su ogni macchina; niente macOS; Linux provato solo in WSL2; licenza non decisa.
- Il bisogno non è ancora stato confrontato con git-fetch-file e peru sul caso reale (`project.md` §2.4): non si sa quanta parte del problema coprono già.

**Che cosa è cambiato davvero.** Il rischio del progetto. Prima la parte più difficile, il merge senza stato salvato, era una scommessa; ora è fatta e non ha richiesto di toccare il modello. La fase 2 parte da una base che regge, e il suo dubbio principale resta uno solo: A-01, cioè se gli hosting accettano `refs/shared/*`.

**Che cosa alzerebbe l'impatto, in ordine di resa.**
1. ~~Comandi di manutenzione e silenzi~~: fatti. Ri-aggancio, `unlink`, `prune`, `branch` e `relocate` ci sono (D-080…D-084); un file sospeso perché il suo branch non esiste più viene segnalato, e un comando su un file in attesa dice perché non fa nulla (D-085).
2. ~~Linux compilato e provato~~: fatto (WSL2, Ubuntu 22.04, git 2.34 e 2.56.1). Resta B-12 (fine riga) da decidere.
3. La prova di A-01: decide se la fase 2 esiste.
4. Un confronto pubblico e onesto con git-subrepo, Copybara e peru.

## 3. Facilità d'uso: il caso normale

### 3.1 Quanto costa in comandi

| Che cosa vuoi fare | Comandi in più rispetto a git | Note |
|---|---|---|
| Condividere un file | 1: `git shared set -c <file>` | Senza `-c` serve anche il commit di `.gitshared` |
| Collegarlo in un'altra repo | 1: `git shared link -c <path del file nell'altra repo>` | Il path è quello che si vede su disco: nessun ID da copiare |
| Mandare una modifica agli altri | 1: `git shared publish`, dopo il normale commit | Il commit da solo non pubblica mai |
| Riceverla in un'altra repo | 1 per repo: `git shared update -c` | Va lanciato dentro ogni repo |
| Sapere a che punto sei | 1: `git shared status` | Gli hook avvisano dopo checkout, merge e rebase |
| Unire due modifiche senza conflitto | 0 in più: lo fa `update` | Poi `publish` |
| Risolvere un conflitto | 3: correggi, `git add`, `git shared update -c`, poi `publish` | Come un conflitto di git, più un passo |

Con N repo, una modifica costa **N + 1 comandi**: un `publish` e un `update` in ognuna delle altre. È il prezzo della regola "ricevere non è applicare" (D-015): nessuno si trova un file cambiato senza averlo chiesto.

### 3.2 Che cosa aiuta

- **Il file resta un file.** Si apre, si compila, si committa come sempre. Chi non conosce lo strumento non se ne accorge.
- **Somiglia a git.** Opzioni, messaggi, codici di uscita, `-h`. Il conflitto è proprio quello di git: `UU` in `git status`, marker nel file, `git add` per risolvere, `git restore` per rinunciare. Gli stage nell'index sono quelli standard, quindi `git mergetool` dovrebbe funzionare (non provato).
- **Dice sempre il passo successivo.** Ogni stato ha il suo comando nel messaggio ("run 'git shared update'").
- **Si annulla con git.** `set` e `link` prima del commit, un `update` non committato, un'unione in corso: basta `git restore`.
- **Non blocca e non rallenta.** Nessun hook sui commit; `status` sta sotto i 120 ms su Windows.
- **Protegge il lavoro.** `update` non sovrascrive modifiche non committate, nemmeno per unire.

### 3.3 Che cosa pesa

- **Dieci stati**, più *sospeso* che si sovrappone agli altri. Nel caso normale se ne vedono quattro (allineato, da pubblicare, da applicare, da committare); gli altri sei si incontrano raramente e vanno capiti proprio quando qualcosa non torna.
- **Tre posti in cui "vive" il file**: il branch (H), l'index (I) e la catena delle versioni (T). Chi conosce bene git li riconosce; chi usa git solo con add, commit e push fa fatica a capire perché un file committato è "da pubblicare".
- **Due passi per finire**: commit e poi `publish`. Dimenticare il secondo non dà errori: gli altri restano indietro in silenzio.
- **`status` guarda solo la repo corrente**, salvo con `--peers` (D-086), che dice per ogni peer se ha la stessa versione, una più vecchia (`publish`) o una più nuova (`update`). Dopo un'unione e il commit `status` da solo dice *allineato* anche se i peer non hanno ancora la versione: bisogna sapere di chiedere `--peers`.
- **Il completamento "al primo comando".** Dopo il commit di un `set` o `link` fatto senza `-c`, i peer non sanno nulla finché non lanci un qualunque `git shared`. È corretto ma non è intuitivo.
- **Messaggi solo in inglese.** Il manuale invece c'è anche in italiano (`git shared lang it`): una pagina per comando, leggibile anche in `help/` (D-087, D-088).

## 4. Facilità d'uso: le trappole

Situazioni in cui si arriva senza aver sbagliato nulla, con l'uscita che c'è oggi.

| Situazione | Che cosa vede l'utente | Uscita oggi | Bug |
|---|---|---|---|
| `set` o `link`, poi `git stash`, poi un comando `git shared`, poi `stash pop` | "discarded pending operation", file *unlinked* | Rilanciare lo stesso `set` o `link`: riprende lo stesso ID | B-40, chiuso |
| Repo clonata | *unlinked* | `git shared link <path del file in un peer>`: ricollega subito, senza toccare il file. Da evitare `set`, che crea una catena separata | chiuso (D-080) |
| Branch nato prima del `set` | *inconsistent*, e il tool suggerisce `unlink` | Ignorarlo, o portare `.gitshared` sul branch. `unlink` lì non smonta nulla: rifiuta e indica il branch giusto | B-16 |
| Il file è shared solo sul branch del `set`; su `main` dopo il merge è *suspended* | Nessun avviso, finché quel branch esiste (è una scelta: potrebbe essere voluto) | `git shared branch <file> -a main` | chiuso (D-083) |
| Branch rinominato (`git branch -m`) | *suspended*, con una riga `note` in `status` e un avviso dagli hook, da `update` e `publish` | Il comando suggerito: `git shared branch <file> -a <nuovo>` | chiuso (D-085) |
| `publish <file>` o `update <file>` su un file in attesa o scollegato | Errore con il motivo e codice 1; senza argomento, una riga per il file saltato | Quella suggerita | chiuso (D-085) |
| Un peer cancellato | "peer not reachable" a ogni `publish` e `update` | `git shared prune`: lo toglie qui e negli altri peer | chiuso (D-082) |
| Una repo spostata | Al primo comando: "this repository was at …" | `git shared relocate` | chiuso (D-084) |
| Una repo copiata (`cp -r`, backup) | Lo stesso avviso; `relocate` rifiuta, perché l'originale c'è ancora | `git shared relocate --copy`: la copia diventa un progetto in più | chiuso (D-084) |
| Voler smettere di condividere un file | — | `git shared unlink <file>` e commit; gli altri progetti dimenticano questo | chiuso (D-081) |
| `link`, ritocco del file, commit | *diverged*, poi un conflitto sull'intero file | Risolvere il conflitto | B-41 |
| Risolvere un conflitto tenendo esattamente la propria versione | L'unione risulta abbandonata, di nuovo *diverged* | Cambiare almeno un carattere | limite di D-078 |
| Committare la risoluzione prima di rilanciare `update` | Di nuovo *diverged* | Rilanciare `update`: di solito unisce da solo | limite di D-078 |
| Unione abbandonata con `git restore`, poi una modifica qualsiasi messa in staging | *merge in progress*; `update -c` prende la modifica per la risoluzione e la versione perde le righe del peer, in silenzio | Dopo l'abbandono lanciare subito `git shared update`, che toglie l'unione scaduta | B-48 |
| Catena locale persa (`push --mirror` da un clone, `fetch --prune` con `+refs/*:refs/*`) | *inconsistent*, e il consiglio è sbagliato; `update` fallisce | Solo `unlink`, che smonta la condivisione | B-51 |

Nessuna di queste quindici situazioni richiede di modificare `.git/config` a mano. Una sola resta muta: B-48, finché non è corretto. L'altro silenzio è voluto: un file sospeso su un branch che esiste ancora non avvisa, perché può essere una scelta.

## 5. Giudizio per tipo di utente

| Utente | Caso normale | Quando qualcosa non torna |
|---|---|---|
| L'autore, o chi conosce bene git (index, ref, rebase) | **Facile.** Cinque comandi, logica riconoscibile | **Gestibile.** Sa leggere `.git/config` e le ref |
| Sviluppatore che usa git ogni giorno senza conoscerne gli interni | **Facile**, dopo aver letto il breef una volta | **Gestibile, con qualche intoppo.** Ogni stato dice il comando da lanciare (*unlinked* si risolve con `link`); confonde ancora *inconsistent* sui branch nati prima del `set` (B-16) |
| Chi usa git solo da interfaccia grafica | **Medio.** Deve aprire un terminale per `publish` e `update` | **Molto difficile** |

## 6. Robustezza: su che cosa poggia il giudizio

**Punti solidi.**
- Lo stato si ricava ogni volta dal contenuto della repo: hook saltati, rebase, reset non lo corrompono.
- Ogni avanzamento della catena è condizionato e solo in fast-forward: una versione non viene mai sovrascritta.
- Il contenuto del file non passa mai dai buffer del programma: viaggia come blob tra comandi git.
- 845 test di sistema, che lanciano i comandi come farebbe l'utente.

**Punti deboli, da sapere.**
- **Nessun bug grave aperto**: B-46, trovato dalla seconda ricerca (§9), è chiuso (D-091). Restano 34 problemi tra medi e minori (`reportIdea.md` §2), tra cui tre nelle unioni tra versioni (B-47, B-48, B-49).
- **Linux provato solo in WSL2** (Ubuntu 22.04, git 2.34 e 2.56.1, GCC 11): tutti i test passano, ma nessun uso reale su una macchina Linux.
- **Fine riga** (B-12): tra repo con normalizzazioni diverse ogni versione risulterebbe una modifica.
- **La base si ricava dai blob nella storia.** Uno squash la cancella: il file risulta *diverged* anche se non lo è. Con il merge si chiude da solo, ma l'utente vede comunque uno stato che non si aspetta (B-42). Il caso contrario, revert più squash, riporta in silenzio la versione che si voleva togliere.
- **Nessun test di unità** e nessuna prova con più di 8 peer.
- **Costo delle unioni non misurato**: un'unione pulita lancia circa 7 processi git in più; su Windows sono dell'ordine di 200 ms, stimati e non misurati.

## 7. Che cosa farei per l'uso quotidiano, in ordine

1. ~~C-20~~ e ~~ri-aggancio dopo un clone~~: fatti (D-080).
2. ~~`unlink`~~, ~~`prune`~~, ~~`branch`~~, ~~`relocate`~~, ~~nota per i branch spariti~~: fatti (D-081…D-085).
3. ~~B-37~~, ~~`status --peers`~~, ~~B-46~~: fatti (D-085, D-086, D-091).
4. **Unioni tra versioni: B-47, B-48, B-49** (§9). Sono gli ultimi problemi che possono far perdere o confondere una versione.
5. **B-16.** Toglie *inconsistent* dai branch vecchi, lo stato che confonde di più.
6. **Un `update` che dopo il commit dell'unione pubblica da solo** (o lo propone): chiude il "due passi per finire" nel caso in cui dimenticarlo costa di più.
7. **Usarlo davvero per qualche settimana** sugli header reali, annotando ogni volta che serve aprire `.git/config`. È la misura che manca a questo report.

## 8. Quanto manca a una versione buona per l'uso locale

> **Stima del 2026-11-17, aggiornata dopo D-091** (845 test, `project.md` v2.29). È una stima, non una misura: si basa sui bug aperti di `reportIdea.md`, sulle prove fatte con l'eseguibile e sul ritmo di lavoro degli ultimi giorni.

### 8.1 Che cosa vuol dire "buona"

Una versione che l'autore, e un collega che conosce git, possono usare ogni giorno sugli header reali senza sorprese. In concreto, cinque condizioni verificabili:

1. **Nessun vicolo cieco e nessun silenzio** nell'uso comune: ogni stato ha un comando che ne fa uscire, ogni comando dice cosa ha fatto. *Oggi: raggiunto* (D-078…D-086), salvo il silenzio di B-48 (§4, §9).
2. **Nessuno stato sbagliato** dovuto alla configurazione o all'ambiente dell'utente (config git, hook esistenti, path con accenti). *Oggi: no* (B-32, C-02, C-05).
3. **Le due piattaforme dichiarate funzionano**: tutti i test passano su Windows e su Linux. *Oggi: raggiunto* (D-089, D-090).
4. **L'aiuto standard funziona**: `git shared --help` apre il manuale invece di un errore. *Oggi: raggiunto* (D-087).
5. **Provata nell'uso vero**: qualche settimana sugli header reali, senza dover aprire `.git/config` a mano.

### 8.2 A che punto siamo

| Area | Stato | Commento |
|---|---|---|
| Funzioni | **~95%** | Ciclo completo, unione delle divergenze, manutenzione (`unlink`, `prune`, `branch`, `relocate`), `status --peers`. Mancano solo funzioni non essenziali (`mv`, `link` su un file esistente, pattern dei branch) |
| Correttezza su Windows | **~85%** | Nessun bug grave (B-46 chiuso, D-091). Restano le unioni tra versioni (B-47…B-49) e casi medi legati all'ambiente (§8.3) |
| Linux | **~85%** | Provato in WSL2 (Ubuntu 22.04) con git 2.34, il minimo di D-032, e con 2.56.1, l'ultima stabile: compila senza warning, tutti i test passano con entrambe (845 dopo D-091). Compatibilità chiusa (D-090). Mancano le misure (punto 9) e l'uso su una macchina Linux vera |
| Documentazione per l'utente | **~90%** | Pagina per comando in `help/`, anche in italiano (`git shared lang it`); `git shared -h` spiega come aprire il manuale; `git shared --help` funziona dopo `make install` (su Windows serve copiare la pagina HTML con i permessi di amministratore). Messaggi dei comandi solo in inglese |
| Uso reale | **0%** | Solo test automatici |

**Complessivamente: circa tre quarti della strada.** La parte più difficile (modello, merge, manutenzione) è fatta; quello che resta è soprattutto verifica, ambiente e rifinitura.

### 8.3 Che cosa manca, in ordine

Unità di misura: una **sessione** è un blocco di lavoro come quelli degli ultimi giorni (una funzione o un gruppo di correzioni, con test, documenti e commit).

**A. Indispensabili**

| # | Lavoro | Bug | Stima | Perché è indispensabile |
|---|---|---|---|---|
| 0 | Unioni tra versioni (§9): ~~B-46~~ fatto (D-091); restano B-47 (avanzamento non fast-forward), B-48 (unione abbandonata che torna viva), B-49 (versione doppia), con i test in `t0009` | B-47…B-49 | 1–1,5 | B-48 perde in silenzio le righe di un peer; B-47 viola il solo fast-forward |
| 1 | Stato indipendente dalla config git: `--root` e merge fissati in `find_base`, o `rev-list` + `diff-tree` | B-32 | 0,5 | Con `log.showRoot=false` un file si vede *diverged* e `publish` rifiuta: stato sbagliato da una preferenza dell'utente |
| 2 | Branch nati prima del `set`: sezione assente sul branch = file non presente qui, non *inconsistent* | B-16 | 1 | È lo stato che confonde di più e propone un comando sbagliato |
| 3 | Hook non `sh` (Python, binari): non inserire il blocco, avvisare | C-02 | 0,5 | Oggi un hook Python viene rotto da `set` |
| 4 | Windows: git in un path con accenti, `PATH` vuoto | C-05, C-10 | 0,5 | Con `C:\Users\José` nessun comando parte |
| 5 | ~~`git shared --help`~~: fatto (D-087), con una pagina per comando in `help/` | REQ-F-019 | – | – |
| 6 | ~~**Linux**: compilare e far passare i test~~: fatto in WSL2. Due hook di prova senza `chmod +x` in t0006; un bug sul modo degli hook dell'utente (C-25, D-089). C-19 provato: senza effetti con git | C-19 | – | – |
| 7 | Fine riga e bit di esecuzione: `link` confronta gli attributi `text`/`eol` con la sorgente e avvisa; il modo `100755` si conserva | B-12, C-11 | 1–1,5 | Tra repo con normalizzazioni diverse ogni versione sembra una modifica e i merge diventano conflitti sull'intero file |
| 8 | Minori che si incontrano davvero: `set .gitshared`, cartella hook assente, `config --unset` con più valori, path assoluti, maiuscole su Windows, `link` interrotto, staging non protetto, avviso se la sorgente non è allineata, `link` da un worktree secondario | B-36, C-04, C-09, C-13, C-12, C-21, B-43, B-44, B-50 | 1,5–2 | Ognuno è raro, insieme sono la differenza tra "funziona" e "non sorprende" |
| 9 | Misure: `status` con molti file, `publish` verso 32 peer, costo di un'unione; su Windows e Linux | REQ-NF-007 | 0,5 | Le soglie sono requisiti; oggi misurate solo fino a 8 peer |
| 10 | ~~Riallineare i documenti~~: fatto il 2026-11-17 (`breef.md`, `project.md`, questo report). Restano le incoerenze di `reportIdea.md` §8 che si chiudono nel codice | – | – | – |
| | **Totale A** | | **6,5–8 sessioni** | |

**B. Uso reale**

| # | Lavoro | Stima |
|---|---|---|
| 11 | Usarlo sugli header reali per 2–4 settimane di calendario, annotando ogni sorpresa e ogni volta che serve `.git/config` | poco impegno, ma tempo di calendario |
| 12 | Correggere quello che emerge | 1–2 sessioni (stima prudente: l'uso vero trova sempre qualcosa) |

**C. Utili ma non necessari per "buona"**

- B-41 (base nota al `link`), B-35 (fetch prima di `publish`), B-09 (commit della catena deterministici): riducono merge e conflitti evitabili, ma oggi c'è sempre un'uscita.
- `update` che dopo il commit di un'unione propone `publish`: chiude il "due passi per finire" (§3.3).
- Messaggi in italiano, test di unità e matrice di tracciabilità completa (`reportIdea.md` §7 punto 11): importanti per un processo V-cycle, non per l'uso.
- C-14, C-15, C-16, C-18, C-08, B-38: qualità del codice e casi limite.
- B-51 (catena locale persa: `update` che la rifà dai peer) e C-26 (`chain_blobs` che parte dalla T già letta): casi rari, oggi senza perdita.

### 8.4 In sintesi

| | Sessioni | Calendario |
|---|---|---|
| Indispensabili (A) | 6,5–8 | 1–2 settimane al ritmo attuale |
| Uso reale e correzioni (B) | 1–2 | 2–4 settimane, in parallelo al resto |
| **Versione buona per l'uso locale** | **circa 8–10** | **circa un mese** |

Tappe proposte, con un tag ciascuna:

- **`v0.3.0`**: punti 0–4 e 8. Unioni tra versioni corrette, stato giusto anche con config e ambienti diversi (`--help` è già fatto).
- **`v0.4.0`**: punti 7 e 9. Fine riga e bit di esecuzione gestiti, prestazioni misurate (Linux è già provato).
- **`v1.0.0-locale`**: dopo l'uso reale (punti 11–12).

**Il rischio maggiore era Linux** (punto 6): provato in WSL2 il 2026-11-17, non ha mostrato problemi di fondo. Ora il rischio maggiore è l'uso reale, che per definizione trova cose che i test non cercano.


## 9. Seconda ricerca di bug: flusso logico e struttura (2026-11-17)

> **Come.** Rilettura di `breef.md`, `project.md` (§5, §6), `reportIdea.md` e di tutto `src/`, cercando i punti in cui il modello promette qualcosa che il flusso non mantiene. Ogni ipotesi è stata provata con l'eseguibile di `build/` (commit `667122c`) in repo temporanee, Windows, git 2.43. "Provato" = riprodotto; "Lettura" = ricavato dal codice. ID nella numerazione di `reportIdea.md` (B = modello o comportamento, C = codice; gravità A/M/B come lì).

### 9.1 Elenco

| ID | Gr. | Problema | Verifica | Dove |
|---|---|---|---|---|
| B-46 | **A** | La versione di un peer unita da un'altra repo non è riconosciuta come versione: il peer la vede *divergente*, e il suo `update` reintroduce in silenzio ciò che l'altra repo aveva tolto | Provato; **chiuso** (D-091) | `chain_blobs`, `find_base`, `cmd_has_local_changes` |
| B-47 | M | `update`: T avanza su una versione ricevuta che non la contiene (avanzamento non fast-forward) | Provato | `receive` in `cmd_update.c` |
| B-48 | M | Unione abbandonata con `git restore`, poi una modifica qualsiasi in staging: `update` la prende per la risoluzione e scarta le modifiche del peer | Provato | `merge_is_live`, `settle_merge` |
| B-49 | B | Ogni conclusione di un conflitto tra versioni crea una seconda versione di unione, ridondante | Provato | `update_entry`, `merge_step` |
| B-50 | M | L'identità di una repo è il path del worktree da cui si lancia il comando: un `link` da un worktree secondario lega i peer a quel path, e alla sua rimozione la repo esce dal gruppo senza che nessuno lo noti | Provato | `find_source`, `complete_link`, `peers_announce` |
| B-51 | B | Catena locale `refs/shared/<id>` assente: stato *incoerente*, `update` fallisce, `link` rifiuta; l'unica uscita è `unlink`, che smonta la condivisione | Provato | `state_derive`, `receive`, `cmd_link` |
| C-26 | B | `chain_blobs` rilegge `refs/shared/<id>` per nome invece di partire dalla T già letta: B-31 è chiuso solo a metà | Lettura | `state_derive`, `cmd_has_local_changes` |

### 9.2 Schede

**B-46 — La versione del peer, dopo un'unione, non è più una versione** (provato; gravità A)
- **Chiuso il 2026-11-17 (D-091).** `chain_blobs` visita il grafo della catena con entrambi i genitori, sullo stesso `cat-file`. Il test chiesto qui sotto è in `t0009`: con l'eseguibile precedente 6 dei 13 test nuovi falliscono, tra cui la riga rimessa in silenzio.
- *Scenario.* A e B allineati a v1. A pubblica vA mentre B non è raggiungibile. B modifica la riga 4 e pubblica: crea vB, A la rifiuta (B-35). A lancia `update`: unione pulita, versione M con genitori (vA, vB), commit, `publish`: B accetta M in fast-forward, perché M discende da vB. Poi A decide che la riga di B è sbagliata, la toglie, committa e pubblica V2.
- *Che cosa succede in B.* H = contenuto di vB, T = V2, che discende da vB. Lo stato giusto è *da applicare*; `status` dice *diverged* e `publish` rifiuta. `update -c` fa un'unione a 3 vie con base v1: la riga 4 è cambiata solo "dalla parte di B" e torna `l4-B`. Nasce una versione nuova, B la pubblica, A la applica con `update`: **la rimozione fatta da A è annullata in silenzio in entrambe le repo** (provato, contenuto finale `l4-B` in A e in B). Viola la regola 5 del breef ("Nessuna modifica viene scartata").
- *Anche senza la rimozione:* B vede *diverged* per una versione che contiene già la sua, e `update` stampa "merged … run 'git shared publish'" anche quando il risultato è T e non c'è niente da pubblicare. Se A, risolvendo un conflitto, ha scelto righe diverse da quelle di B, B si ritrova in conflitto su ciò che A ha già risolto.
- *Causa.* `chain_blobs` segue solo il primo genitore (`<commit>^`): il secondo genitore di un'unione, cioè la versione del peer, non entra mai nella lista delle versioni. `find_base` non la trova nella storia di B e prende come base la versione comune più vecchia (v1). Lo stesso elenco serve a `cmd_has_local_changes`: un index uguale alla versione del peer viene preso per una modifica locale (lettura).
- *Rimedio possibile.* Leggere la catena con tutti i genitori (un `git rev-list --topo-order refs/shared/<id>` o la visita di `^1` e `^2` sullo stesso `cat-file`) e ricavare la base sul grafo: se H è il blob di una versione raggiungibile da T, quella è la base, e lo stato è *da applicare*. Serve un test in `t0009`: "il peer la cui versione è stata unita riceve l'unione e risulta *da applicare*".

**B-47 — `update` sposta T su una versione che non la contiene** (provato)
- *Scenario.* A con peer [B, C], nell'ordine. A ha T = vA. C ha vC, che discende da vA; B ha vB, nata da v1 (indipendente da vA). `update` in A riceve vB e vC.
- *Che cosa succede.* `merge-base --independent vA vB vC` restituisce vB e vC, nell'ordine di ingresso (provato: git conserva l'ordine). vA non c'è, quindi `receive` conclude che "una la contiene" e fa avanzare T su `tops.v[0]`, cioè vB, che vA non la contiene: `update-ref refs/shared/<id> vB vA` (visto con `GIT_TRACE`). Poi il contenuto di A (vA) è trattato come una modifica del branch da unire a vB, e la versione che ne nasce non ha vA tra i genitori; solo l'unione successiva con vC la riporta nel grafo. Se la prima unione va in conflitto, la catena locale resta su vB: vA, pubblicata da A, non è più raggiungibile dalla sua catena. Il messaggio è "new version received".
- *Causa.* `cmd_update.c`, `receive`: `tops.v[0]` è preso senza controllare che discenda da T. Viola REQ-NF-005 e l'invariante di §6 ("ogni avanzamento della catena è condizionato e solo in fast-forward"): `update-ref` controlla il valore vecchio, non la discendenza.
- *Rimedio possibile.* Scegliere tra le versioni rimaste quella per cui `merge-base --is-ancestor T <versione>` è vero (un processo per candidata, solo quando sono più di una).

**B-48 — Un'unione abbandonata torna viva con la prima modifica in staging** (provato)
- *Scenario.* Conflitto tra vA (locale) e vB (peer); l'utente rinuncia come dice il manuale: `git restore -s HEAD -SW -- f.h`. `status` dice *aligned*, ma `refs/shared-merge/<id>` resta (la toglie solo `update`). L'utente lavora sul file (riga 6) e fa `git add`: `status` dice **merge in progress**. `update -c` "conclude l'unione" con quella modifica: nasce una versione con genitori (vA, vB) **senza le righe di vB**, e `publish` la invia.
- *Effetto.* Ogni peer che non è B riceve una versione che dichiara di contenere vB ma ne ha perso le modifiche. In B l'effetto si somma a B-46 (non provato). È una modifica scartata in silenzio, partendo dalla procedura di abbandono documentata.
- *Causa.* `merge_is_live` considera viva l'unione se l'index è diverso da H: non distingue una risoluzione da una modifica fatta dopo l'abbandono. `status` vede la ref scaduta e non la toglie.
- *Rimedio possibile.* Togliere la ref appena un comando qualsiasi (anche `status`) la vede scaduta (index = H, nessuno stage); in più, concludere solo con un atto esplicito (es. `update --continue`) o almeno avvisare quando la risoluzione non contiene nessuna modifica della versione del peer rispetto alla base.

**B-49 — Versione di unione doppia** (provato)
- Conflitto tra vA e vB, risolto con un contenuto nuovo, `git add`, `update -c`. La catena riceve due versioni con lo stesso tree: `879df60` (genitori vA, vB) e subito dopo `1fe7fa2` (genitori `879df60`, vB). Succede a ogni conclusione di un conflitto tra versioni.
- *Causa.* `update_entry` calcola le versioni da unire (`heads`) in `receive`, prima che `settle_merge` concluda l'unione; poi le unisce di nuovo. `merge_step` crea una versione ogni volta che c'è un secondo genitore, anche se è già antenato di T e il contenuto non cambia.
- *Rimedio possibile.* Saltare nel ciclo le versioni già antenate di T (il merge-base è la versione stessa); in `merge_step`, nessuna versione nuova se `p2` è antenato di `tip`.

**B-50 — Peer registrato con il path di un worktree secondario** (provato; `reportIdea.md` §7 punto 12 lo dava "non provato")
- *Scenario.* A ha un worktree secondario `A-wt` (`git worktree add`). B fa `git shared link -c ../A-wt/f.h`. B registra come peer `A-wt`, non `A`; A registra B. Dopo `git worktree remove A-wt`: `publish` in B dice "peer A-wt not reachable", `prune` lo toglie come "it no longer exists". B non ha più peer; A continua a elencare B e `status --peers` in A dice solo "newer or different version". Il gruppo si è diviso senza un errore.
- *Causa.* L'identità di una repo è `rev-parse --show-toplevel` del worktree da cui si lavora: vale per la sorgente di `link` (`find_source`) e per `self` nell'annuncio e nella pulizia (`complete_link`, `repo_forget`), anche quando il comando gira in un worktree secondario. Config e ref sono comuni ai worktree, quindi lo stesso repository può comparire con più nomi.
- *Rimedio possibile.* Usare come identità la radice del worktree principale (da `rev-parse --git-common-dir`), sia per la sorgente sia per `self`; oppure rifiutare `link` e il completamento di un'attesa da un worktree secondario.

**B-51 — Catena locale persa: nessuna uscita** (provato)
- Con la sezione locale presente e `refs/shared/<id>` assente: `status` dice *inconsistent* (che il breef spiega come "rinominato o cancellato fuori da git shared": consiglio sbagliato), `update` esce con "cannot update the version chain", `link` risponde "already shared here". Resta solo `unlink`, che al commit avvisa i peer e smonta la condivisione.
- *Come ci si arriva.* `git push --mirror` da un clone verso la repo (cancella `refs/shared/*`, `reportIdea.md` §5), un `fetch --prune` con refspec `+refs/*:refs/*`, una cancellazione a mano.
- *Rimedio possibile.* `update` rifà il fetch dai peer quando la catena manca, come già fa il completamento di `link` con la ref temporanea; `status` con un messaggio proprio ("catena assente: run 'git shared update'").

**C-26 — B-31 chiuso a metà** (lettura)
- `state_derive` legge il commit di T e il suo blob, ma poi `chain_blobs` riparte dal nome `refs/shared/<id>`: se un peer invia una versione nel frattempo, la base è calcolata su una catena diversa da quella di `si.tip`. Le scritture restano condizionate (`update-ref` con il valore vecchio), quindi non si perde nulla: si ottiene un errore invece dello stato giusto. Stesso schema in `cmd_has_local_changes`.
- *Rimedio.* `chain_blobs` riceve il commit di partenza invece dell'ID.

### 9.3 Che cosa cambia nel giudizio

- **La parte da rivedere è l'unione tra versioni** (D-078). Il percorso provato dai test è "una repo unisce e pubblica"; non c'è un test sul **peer la cui versione è stata unita**, che è proprio il caso in cui nascono B-46 e B-49. B-46, B-47 e B-48 hanno la stessa radice: la catena è un grafo, ma il codice la legge come una lista (primo genitore) e decide "chi contiene chi" senza chiederlo a git.
- **Effetto sulla stima di §8**, già riportato in §8.3. Le unioni tra versioni sono il punto 0 degli indispensabili (A): B-46 è fatto, B-47…B-49 sono stimati 1–1,5 sessioni. B-50 rientra nel punto 8 (minori che si incontrano davvero), B-51 e C-26 nei non necessari (C).
- **Cautele nell'uso reale, finché B-47 e B-48 restano aperti:** lanciare `git shared update` prima di ogni `publish` (rende raro B-35, da cui nascono le divergenze tra versioni) e, dopo aver abbandonato un'unione con `git restore`, lanciare subito `git shared update`, che toglie l'unione scaduta.
