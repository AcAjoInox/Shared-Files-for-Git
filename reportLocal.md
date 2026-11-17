# Report sull'uso locale — git shared

> **2026-11-16.** Baseline `project.md` v2.24 (D-001…D-086). Codice v0 con unione delle divergenze, ri-aggancio, comandi di manutenzione completi e `status --peers`: 718 test su 718, Windows, git 2.43. Riguarda solo la fase 1 (repo sulla stessa macchina). I bug sono in `reportIdea.md`; qui ci sono due giudizi: l'impatto atteso e la facilità d'uso.
>
> **Da dove vengono i giudizi.** Dalla lettura del codice, dai test e dalle prove fatte a mano con l'eseguibile durante la ricerca dei bug. Nessuno, a parte l'autore, lo ha ancora usato: la parte sull'uso è una valutazione ragionata, non una misura.

## 1. In breve

- **L'impatto atteso sulla community non è cambiato: resta di nicchia.** Il motivo è lo stesso di prima: la fase 1 funziona solo tra repo della stessa macchina, quindi non si può condividere con un collega.
- **È cambiato il valore per chi lo usa da solo.** Prima del merge una divergenza era un vicolo cieco; ora ogni stato ha un comando che ne fa uscire. Da "prototipo che funziona nei casi buoni" è diventato "strumento usabile ogni giorno da chi lo conosce".
- **È cambiata la credibilità come dimostrazione.** La promessa che lo distingue dagli altri strumenti ("nessuna modifica si perde, le divergenze si uniscono", `project.md` §2.3 punto 5) ora è codice provato, non un requisito.
- **L'uso quotidiano è semplice**: cinque comandi, uno o due in più rispetto al normale lavoro con git. **L'uso fuori dal caso normale è meno semplice**: dieci stati, e alcune trappole in cui si finisce senza aver sbagliato nulla (§4).
- **Le trappole più probabili sono chiuse** (B-39, B-40, C-20, clone: D-079, D-080). Prima di usarlo sul lavoro vero restano da sapere quelle di §4, che si chiudono con i comandi di manutenzione mancanti.

## 2. È migliorata l'aspettativa di impatto?

| Aspetto | Prima del merge | Adesso | Cambia? |
|---|---|---|---|
| Uso personale, più repo sulla stessa macchina | Funziona finché nessuno modifica da una versione vecchia; poi "merging is not implemented yet" | Ciclo completo: modifica, pubblicazione, applicazione, unione, conflitto | **Sì, molto** |
| Robustezza | 1 dato salvato fragile previsto (merge in `.git/`), vicoli ciechi con squash e amend | Stato sempre ricavato; l'unica ref salvata vale solo se l'index la conferma; squash e amend si chiudono con un `update` | **Sì** |
| Valore come dimostrazione ("un file condiviso con una ref e il solo trasporto git") | Idea documentata, metà realizzata | Realizzata per il locale, con 718 test e un registro di 86 decisioni | **Sì** |
| Adozione da parte di altri | Di nicchia | Di nicchia | **No** |
| Uso in un gruppo | Impossibile (una macchina) | Impossibile (una macchina) | **No** |

**Perché l'adozione non cambia.**
- Chi cerca "condividere un file tra repo" quasi sempre ha un collega o una CI dall'altra parte. La fase 1 non li raggiunge.
- Ogni clone va ricollegato a mano con un `link` (ora funziona, ma va saputo): niente segue la repo da sola.
- Va installato a mano su ogni macchina; niente macOS; Linux mai compilato; licenza non decisa.
- Il bisogno non è ancora stato confrontato con git-fetch-file e peru sul caso reale (`project.md` §2.4): non si sa quanta parte del problema coprono già.

**Che cosa è cambiato davvero.** Il rischio del progetto. Prima la parte più difficile, il merge senza stato salvato, era una scommessa; ora è fatta e non ha richiesto di toccare il modello. La fase 2 parte da una base che regge, e il suo dubbio principale resta uno solo: A-01, cioè se gli hosting accettano `refs/shared/*`.

**Che cosa alzerebbe l'impatto, in ordine di resa.**
1. I comandi di manutenzione sono completi (ri-aggancio, `unlink`, `prune`, `branch`, `relocate`: D-080…D-084) e i silenzi principali sono chiusi (D-085): un file sospeso perché il suo branch non esiste più viene segnalato, e un comando su un file in attesa dice perché non fa nulla.
2. Linux compilato e provato, con B-12 (fine riga) deciso.
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
- **Messaggi solo in inglese** e manuale solo incorporato (`git shared help`).

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

Nessuna di queste tredici situazioni richiede più di modificare `.git/config` a mano, e nessuna resta muta. Il silenzio che rimane è voluto: un file sospeso su un branch che esiste ancora non avvisa, perché può essere una scelta.

## 5. Giudizio per tipo di utente

| Utente | Caso normale | Quando qualcosa non torna |
|---|---|---|
| L'autore, o chi conosce bene git (index, ref, rebase) | **Facile.** Cinque comandi, logica riconoscibile | **Gestibile.** Sa leggere `.git/config` e le ref |
| Sviluppatore che usa git ogni giorno senza conoscerne gli interni | **Facile**, dopo aver letto il breef una volta | **Difficile.** Stati come *inconsistent* o *unlinked* non hanno un comando di uscita |
| Chi usa git solo da interfaccia grafica | **Medio.** Deve aprire un terminale per `publish` e `update` | **Molto difficile** |

## 6. Robustezza: su che cosa poggia il giudizio

**Punti solidi.**
- Lo stato si ricava ogni volta dal contenuto della repo: hook saltati, rebase, reset non lo corrompono.
- Ogni avanzamento della catena è condizionato e solo in fast-forward: una versione non viene mai sovrascritta.
- Il contenuto del file non passa mai dai buffer del programma: viaggia come blob tra comandi git.
- 718 test di sistema, che lanciano i comandi come farebbe l'utente.

**Punti deboli, da sapere.**
- **Nessun bug grave aperto**, 29 tra medi e minori (`reportIdea.md` §2).
- **Solo Windows è provato.** I file `*_posix.c` non sono mai stati compilati.
- **Fine riga** (B-12): tra repo con normalizzazioni diverse ogni versione risulterebbe una modifica.
- **La base si ricava dai blob nella storia.** Uno squash la cancella: il file risulta *diverged* anche se non lo è. Con il merge si chiude da solo, ma l'utente vede comunque uno stato che non si aspetta (B-42). Il caso contrario, revert più squash, riporta in silenzio la versione che si voleva togliere.
- **Nessun test di unità** e nessuna prova con più di 8 peer.
- **Costo delle unioni non misurato**: un'unione pulita lancia circa 7 processi git in più; su Windows sono dell'ordine di 200 ms, stimati e non misurati.

## 7. Che cosa farei per l'uso quotidiano, in ordine

1. ~~C-20~~ e ~~ri-aggancio dopo un clone~~: fatti (D-080).
2. ~~`unlink`~~, ~~`prune`~~, ~~`branch`~~, ~~`relocate`~~, ~~nota per i branch spariti~~: fatti (D-081…D-085).
3. ~~B-37~~, ~~`status --peers`~~: fatti (D-085, D-086).
4. **B-16.** Toglie *inconsistent* dai branch vecchi, lo stato che confonde di più.
5. **Un `update` che dopo il commit dell'unione pubblica da solo** (o lo propone): chiude il "due passi per finire" nel caso in cui dimenticarlo costa di più.
6. **Usarlo davvero per qualche settimana** sugli header reali, annotando ogni volta che serve aprire `.git/config`. È la misura che manca a questo report.

## 8. Quanto manca a una versione buona per l'uso locale

> **Stima del 2026-11-17**, sullo stato di `v0.2.0` + `status --peers` (718 test, `project.md` v2.24). È una stima, non una misura: si basa sui bug aperti di `reportIdea.md`, sulle prove fatte con l'eseguibile e sul ritmo di lavoro degli ultimi giorni.

### 8.1 Che cosa vuol dire "buona"

Una versione che l'autore, e un collega che conosce git, possono usare ogni giorno sugli header reali senza sorprese. In concreto, cinque condizioni verificabili:

1. **Nessun vicolo cieco e nessun silenzio** nell'uso comune: ogni stato ha un comando che ne fa uscire, ogni comando dice cosa ha fatto. *Oggi: raggiunto* (D-078…D-086).
2. **Nessuno stato sbagliato** dovuto alla configurazione o all'ambiente dell'utente (config git, hook esistenti, path con accenti).
3. **Le due piattaforme dichiarate funzionano**: tutti i test passano su Windows e su Linux.
4. **L'aiuto standard funziona**: `git shared --help` apre il manuale invece di un errore.
5. **Provata nell'uso vero**: qualche settimana sugli header reali, senza dover aprire `.git/config` a mano.

### 8.2 A che punto siamo

| Area | Stato | Commento |
|---|---|---|
| Funzioni | **~95%** | Ciclo completo, unione delle divergenze, manutenzione (`unlink`, `prune`, `branch`, `relocate`), `status --peers`. Mancano solo funzioni non essenziali (`mv`, `link` su un file esistente, pattern dei branch) |
| Correttezza su Windows | **~85%** | Nessun bug grave. Restano casi medi legati all'ambiente (§8.3, gruppo B) |
| Linux | **~0% verificato** | Codice `*_posix.c` scritto ma mai compilato; anche i test usano comandi da Git Bash (`pwd -W`) |
| Documentazione per l'utente | **~70%** | Manuale incorporato e `-h` completi; `git shared --help` fallisce ("documentation file not found", provato); `breef.md` e alcune righe di questo report da riallineare |
| Uso reale | **0%** | Solo test automatici |

**Complessivamente: circa tre quarti della strada.** La parte più difficile (modello, merge, manutenzione) è fatta; quello che resta è soprattutto verifica, ambiente e rifinitura.

### 8.3 Che cosa manca, in ordine

Unità di misura: una **sessione** è un blocco di lavoro come quelli degli ultimi giorni (una funzione o un gruppo di correzioni, con test, documenti e commit).

**A. Indispensabili**

| # | Lavoro | Bug | Stima | Perché è indispensabile |
|---|---|---|---|---|
| 1 | Stato indipendente dalla config git: `--root` e merge fissati in `find_base`, o `rev-list` + `diff-tree` | B-32 | 0,5 | Con `log.showRoot=false` un file si vede *diverged* e `publish` rifiuta: stato sbagliato da una preferenza dell'utente |
| 2 | Branch nati prima del `set`: sezione assente sul branch = file non presente qui, non *inconsistent* | B-16 | 1 | È lo stato che confonde di più e propone un comando sbagliato |
| 3 | Hook non `sh` (Python, binari): non inserire il blocco, avvisare | C-02 | 0,5 | Oggi un hook Python viene rotto da `set` |
| 4 | Windows: git in un path con accenti, `PATH` vuoto | C-05, C-10 | 0,5 | Con `C:\Users\José` nessun comando parte |
| 5 | `git shared --help`: pagina HTML (Windows) e man page (Linux) generate dal manuale incorporato e copiate da `make install` | REQ-F-019 | 0,5 | È il primo comando che un utente prova |
| 6 | **Linux**: compilare, far passare i 718 test, correggere ciò che emerge (C-19, `sys_abs_path`, script di test) | C-19 | 1,5–2,5 | Piattaforma dichiarata e mai provata: è il rischio più grande della stima |
| 7 | Fine riga e bit di esecuzione: `link` confronta gli attributi `text`/`eol` con la sorgente e avvisa; il modo `100755` si conserva | B-12, C-11 | 1–1,5 | Tra repo con normalizzazioni diverse ogni versione sembra una modifica e i merge diventano conflitti sull'intero file |
| 8 | Minori che si incontrano davvero: `set .gitshared`, cartella hook assente, `config --unset` con più valori, path assoluti, maiuscole su Windows, `link` interrotto, staging non protetto, avviso se la sorgente non è allineata | B-36, C-04, C-09, C-13, C-12, C-21, B-43, B-44 | 1,5 | Ognuno è raro, insieme sono la differenza tra "funziona" e "non sorprende" |
| 9 | Misure: `status` con molti file, `publish` verso 32 peer, costo di un'unione; su Windows e Linux | REQ-NF-007 | 0,5 | Le soglie sono requisiti; oggi misurate solo fino a 8 peer |
| 10 | Riallineare i documenti: `breef.md` (limiti della v0), §5 di questo report, incoerenze aperte di `reportIdea.md` §8 | – | 0,5 | Il breef è la porta d'ingresso per chi non è l'autore |
| | **Totale A** | | **8–10 sessioni** | |

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

### 8.4 In sintesi

| | Sessioni | Calendario |
|---|---|---|
| Indispensabili (A) | 8–10 | 1–2 settimane al ritmo attuale |
| Uso reale e correzioni (B) | 1–2 | 2–4 settimane, in parallelo al resto |
| **Versione buona per l'uso locale** | **circa 10–12** | **circa un mese** |

Tappe proposte, con un tag ciascuna:

- **`v0.3.0`**: punti 1–5 e 8. Corretta su Windows anche con config e ambienti diversi, `--help` funzionante.
- **`v0.4.0`**: punti 6, 7 e 9. Linux provato, fine riga gestiti, prestazioni misurate.
- **`v1.0.0-locale`**: dopo l'uso reale (punti 11–12) e il riallineamento dei documenti (punto 10).

**Il rischio maggiore è Linux** (punto 6): non è mai stato compilato, quindi la stima può allungarsi se `proc_posix.c` ha problemi di fondo. Il secondo è l'uso reale, che per definizione trova cose che i test non cercano.

> **Nota:** la tabella di §5 è in parte superata. "Stati come *inconsistent* o *unlinked* non hanno un comando di uscita" non vale più: *unlinked* si risolve con `link` (D-080), *inconsistent* con `unlink <id>` o `branch` (D-081, D-083). Resta la confusione di *inconsistent* sui branch vecchi (punto 2).
