# Report sull'uso locale — git shared

> **2026-11-16.** Baseline `project.md` v2.19 (D-001…D-081). Codice v0 con unione delle divergenze, ri-aggancio e `unlink`: 504 test su 504, Windows, git 2.43. Riguarda solo la fase 1 (repo sulla stessa macchina). I bug sono in `reportIdea.md`; qui ci sono due giudizi: l'impatto atteso e la facilità d'uso.
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
| Valore come dimostrazione ("un file condiviso con una ref e il solo trasporto git") | Idea documentata, metà realizzata | Realizzata per il locale, con 504 test e un registro di 81 decisioni | **Sì** |
| Adozione da parte di altri | Di nicchia | Di nicchia | **No** |
| Uso in un gruppo | Impossibile (una macchina) | Impossibile (una macchina) | **No** |

**Perché l'adozione non cambia.**
- Chi cerca "condividere un file tra repo" quasi sempre ha un collega o una CI dall'altra parte. La fase 1 non li raggiunge.
- Ogni clone va ricollegato a mano con un `link` (ora funziona, ma va saputo): niente segue la repo da sola.
- Va installato a mano su ogni macchina; niente macOS; Linux mai compilato; licenza non decisa.
- Il bisogno non è ancora stato confrontato con git-fetch-file e peru sul caso reale (`project.md` §2.4): non si sa quanta parte del problema coprono già.

**Che cosa è cambiato davvero.** Il rischio del progetto. Prima la parte più difficile, il merge senza stato salvato, era una scommessa; ora è fatta e non ha richiesto di toccare il modello. La fase 2 parte da una base che regge, e il suo dubbio principale resta uno solo: A-01, cioè se gli hosting accettano `refs/shared/*`.

**Che cosa alzerebbe l'impatto, in ordine di resa.**
1. `branch`, `prune`, `relocate`: senza, chi rinomina un branch o cancella una repo deve modificare `.git/config` a mano. (Ri-aggancio e `unlink` ci sono: D-080, D-081.)
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
- **`status` guarda solo la repo corrente.** Dopo un'unione e il commit dice *allineato* anche se i peer non hanno ancora la versione; non c'è un modo per vedere lo stato dei peer (`--peers` è fuori dalla v0).
- **Il completamento "al primo comando".** Dopo il commit di un `set` o `link` fatto senza `-c`, i peer non sanno nulla finché non lanci un qualunque `git shared`. È corretto ma non è intuitivo.
- **Messaggi solo in inglese** e manuale solo incorporato (`git shared help`).

## 4. Facilità d'uso: le trappole

Situazioni in cui si arriva senza aver sbagliato nulla, con l'uscita che c'è oggi.

| Situazione | Che cosa vede l'utente | Uscita oggi | Bug |
|---|---|---|---|
| `set` o `link`, poi `git stash`, poi un comando `git shared`, poi `stash pop` | "discarded pending operation", file *unlinked* | Rilanciare lo stesso `set` o `link`: riprende lo stesso ID | B-40, chiuso |
| Repo clonata | *unlinked* | `git shared link <path del file in un peer>`: ricollega subito, senza toccare il file. Da evitare `set`, che crea una catena separata | chiuso (D-080) |
| Branch nato prima del `set` | *inconsistent*, e il tool suggerisce `unlink` | Ignorarlo, o portare `.gitshared` sul branch. `unlink` lì non smonta nulla: rifiuta e indica il branch giusto | B-16 |
| Il file è shared solo sul branch del `set`; su `main` dopo il merge è *suspended* | Nessun avviso, mai | `git config --add shared.<id>.branch main` a mano | comando `branch` fuori v0 |
| `publish <file>` o `update <file>` su un file in attesa o scollegato | Nessun output, codice 0 | `status` | B-37 |
| Un peer cancellato o spostato | "peer not reachable" a ogni `publish` e `update` | Togliere la riga `peer` da `.git/config` a mano | `prune`, `relocate` fuori v0 |
| Voler smettere di condividere un file | — | `git shared unlink <file>` e commit; gli altri progetti dimenticano questo | chiuso (D-081) |
| `link`, ritocco del file, commit | *diverged*, poi un conflitto sull'intero file | Risolvere il conflitto | B-41 |
| Risolvere un conflitto tenendo esattamente la propria versione | L'unione risulta abbandonata, di nuovo *diverged* | Cambiare almeno un carattere | limite di D-078 |
| Committare la risoluzione prima di rilanciare `update` | Di nuovo *diverged* | Rilanciare `update`: di solito unisce da solo | limite di D-078 |

Due di queste dieci situazioni si chiudono solo modificando a mano `.git/config`: è il segno più chiaro di ciò che manca alla v0, cioè i comandi di manutenzione rimasti (`branch`, `prune`, `relocate`).

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
- 504 test di sistema, che lanciano i comandi come farebbe l'utente.

**Punti deboli, da sapere.**
- **Nessun bug grave aperto**, 33 tra medi e minori (`reportIdea.md` §2).
- **Solo Windows è provato.** I file `*_posix.c` non sono mai stati compilati.
- **Fine riga** (B-12): tra repo con normalizzazioni diverse ogni versione risulterebbe una modifica.
- **La base si ricava dai blob nella storia.** Uno squash la cancella: il file risulta *diverged* anche se non lo è. Con il merge si chiude da solo, ma l'utente vede comunque uno stato che non si aspetta (B-42). Il caso contrario, revert più squash, riporta in silenzio la versione che si voleva togliere.
- **Nessun test di unità** e nessuna prova con più di 8 peer.
- **Costo delle unioni non misurato**: un'unione pulita lancia circa 7 processi git in più; su Windows sono dell'ordine di 200 ms, stimati e non misurati.

## 7. Che cosa farei per l'uso quotidiano, in ordine

1. ~~C-20~~ e ~~ri-aggancio dopo un clone~~: fatti (D-080).
2. ~~`unlink`~~: fatto (D-081). **`branch --add`, `prune`.** Tolgono le due uscite "a mano" rimaste in §4.
3. **B-37 e un `status --peers`.** Nessun comando deve restare muto; serve vedere se gli altri hanno la versione.
4. **B-16.** Toglie *inconsistent* dai branch vecchi, lo stato che confonde di più.
5. **Un `update` che dopo il commit dell'unione pubblica da solo** (o lo propone): chiude il "due passi per finire" nel caso in cui dimenticarlo costa di più.
6. **Usarlo davvero per qualche settimana** sugli header reali, annotando ogni volta che serve aprire `.git/config`. È la misura che manca a questo report.
