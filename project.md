# Git shared file

## 1. Scopo del progetto

L'obiettivo è realizzare un'estensione git per supportare una feature attualmente non presente in git: i file condivisi.

L'idea è quella di utilizzare file in maniera condivisa da più progetti (repo) git e, che, su ogni progetto, si possano aggiornare tali files in maniera semi-automatica (stile submodules) richiamando il comando dedicato di aggiornamento. In questo modo è sufficiente modificare un file su un singolo progetto git per riportare le modifiche su tutti i progetti che fanno uso del file condiviso.
Uno degli esempi più eclatanti è quello degli header condivisi tra più progetti. Aggiornarli tutti ogni volta è una gran fatica ed è facile introdurre bug tra i vari aggiornamenti manuali.
l'aggiornamento di un file shared deve produrre sempre un nuovo commit per essere in linea con la filosofia git dello snapshot. Infatti, deve essere sempre possibile il versionamento conologicamente univoco della singola repo.
> Nota (D-007): il commit di aggiornamento è dedicato all'operazione shared ed è confermato dall'utente (o creato subito con `--commit`). Nessun comando committa da solo: `update` prepara il file e il commit lo fa l'utente, oppure il comando stesso con `-c`/`-m` (D-075).

Il progetto sarà scritto in linguaggio C. La scelta deriva dal fatto che git stesso è scritto in C e dal fatto che io, autore del programma, sono abbastanza skillato in C. Saranno usati script per lo più bash o python per i test.
> Nota (D-063): il C non è una scelta assoluta. In architettura si deciderà come combinare programmi C e script `.sh` / `.bat`.

Il progetto nasce da un'esigenza personale e non punta ad un buisness o ad un prodotto commerciabile. Se lo riterrò degno di nota sarà distribuito su GitHub con licenza che ne permetta l'uso e la modifica ma non a scopo commerciale (o forse si non lo so ancora).
> Nota: la licenza è l'ultima cosa da decidere, a progetto terminato e funzionante (A-17).

Sicuramente dovrò partire dalla base di come funziona git internamente, di come si possono scrivere e come funzionano le estensioni.
La stesura del progetto sarà coaudiuvata dal bro Claude, rigorosamente in Italiano, senza fronzoli.

## 2. Stato dell'arte

### 2.1 Il problema è noto
Condividere un singolo file tra più repo è una domanda ricorrente (Stack Overflow, forum). Le risposte tipiche sono:
- **submodule**: lavora su intere directory, eccessivo per un solo file;
- **subtree**: copia una directory intera e la sua storia, stesso limite;
- **symlink / copia manuale**: nessun versionamento della provenienza, alto rischio di disallineamento;
- **package manager** (conan, vcpkg, ...): sovradimensionati per un header.

Il gap è riconosciuto anche a monte: sulla mailing list di git è stata aperta una feature request per condividere file tra repository.

### 2.2 Strumenti esistenti
| Strumento | Linguaggio | Caratteristiche |
|---|---|---|
| git-fetch-file | — | Importa singoli file; manifest `.git-remote-files` con sorgente e commit. **Il più vicino a questo progetto.** |
| gitcommonsync | Python | Sincronizza file e directory comuni tra repo. |
| gitsync | Go | Comandi `diff`/`sync` guidati da config JSON. |
| peru | Python | Importa file da sorgenti esterne (git, hg, svn, tarball) e fissa la versione esatta. |
| vendir (Carvel) | Go | Dichiarativo; prende il controllo dell'intera directory di destinazione. Usato con Renovate per aggiornamenti automatici. |
| git-vendor | Rust | Alternativa a submodule/subtree, in sviluppo. |
| repo-file-sync-action | GitHub Action | Apre PR nelle repo destinatarie. Legato a GitHub. |
| monorepo-git-tools | — | Sincronizzazione bidirezionale tra repo. |

Nessuno strumento si è affermato come standard: il problema è sentito ma la risposta è frammentata.

### 2.3 Elementi distintivi di questo progetto
Nessuno degli strumenti di §2.2 offre insieme tutte queste caratteristiche:

**Modello**
1. **Granularità sul singolo file, con il file vero nella repo.** Chi non ha l'estensione vede, compila e usa il file normalmente (D-018).
2. **Repo alla pari, senza centro.** Nessun server, store o repo "principale": le repo che usano il file si conoscono tra loro come i remote di git, e ognuna può modificarlo (D-004, D-012).
3. **Stesso modello in locale e in remoto.** In locale i peer sono path, in remoto URL; il meccanismo non cambia (D-001, D-050).

**Sicurezza delle modifiche**
4. **Ogni file shared ha una propria storia di versioni dentro git** (`refs/shared/<id>`), trasportata con `git push` e `git fetch` (D-013, D-040).
5. **Nessuna modifica si perde in silenzio.** Si pubblica solo in fast-forward; due modifiche indipendenti risultano divergenti e si uniscono con un merge a 3 vie, presentato come un normale conflitto git (D-009, D-014).
6. **Ricevere non è applicare.** Una repo viene a sapere che esiste una versione nuova senza che i suoi file vengano toccati; l'utente la applica quando vuole e la approva con un commit (D-007, D-015).
7. **Ogni aggiornamento è un commit** nella storia della repo, che resta cronologicamente univoca (§1).

**Robustezza**
8. **Stato sempre ricavabile.** Lo stato di un file shared si calcola dal contenuto della repo, non da dati salvati: hook saltati, merge, rebase, cloni e spostamenti non lo corrompono (D-041). Fanno eccezione due dati salvati, che valgono solo finché il contenuto della repo li conferma: l'operazione in attesa di commit (D-059) e l'unione in corso (D-078).
9. **Resiste a repo cancellate, spostate o clonate**, con comandi dedicati per ricollegarsi (D-035, D-043, D-049).

**Realizzazione**
10. **Comando git nativo** (`git shared ...`) scritto in C, **senza dipendenze di runtime** oltre a git (D-017).
11. **Indipendente dall'hosting e multipiattaforma**: qualsiasi remote git, Linux e Windows.

### 2.4 Valutazione
- Valore personale e didattico: alto.
- Potenziale di adozione: di nicchia; dipende dalla qualità dei punti 5, 6 e 8 di §2.3, che sono quelli che gli strumenti esistenti non coprono.
- Azione preliminare: provare git-fetch-file e peru sul caso reale (header condivisi) per individuare cosa manca concretamente.

### 2.5 Riferimenti
- Feature request mailing list git: https://lore.kernel.ime.usp.br/git/03d601dc3166$0f302170$2d906450$@nexbridge.com/t/
- gitcommonsync: https://pypi.org/project/gitcommonsync/
- gitsync: https://awesome.ecosyste.ms/projects/github.com%2Fnieomylnieja%2Fgitsync
- peru: https://pypi.org/project/peru
- vendir: https://github.com/vmware-tanzu/carvel-vendir
- Renovate + vendir: https://www.jvt.me/posts/2026/02/27/renovate-update-file/
- git-vendor: https://docs.rs/crate/git-vendor/0.2.0/source/README.md
- repo-file-sync-action: https://github.com/BetaHuhn/repo-file-sync-action
- monorepo-git-tools: https://github.com/nikita-skobov/monorepo-git-tools
- git-fetch-file: https://feedbagel.com/post/utility-for-fetching-and-syncing-individual-files-from-git-repositories

## 3. Vincoli di git su cui poggia il progetto
> Lo studio degli interni di git (strutture, formati su disco, laboratori) è stato rimosso: non serve più. Restano solo i fatti a cui le decisioni rimandano.

- **Le strutture interne di git non sono un'API.** `libgit.a` non è installata e cambia tra versioni: a git si accede solo tramite i suoi comandi (D-017).
- **Plumbing e porcelain.** I comandi plumbing hanno output stabile e sono fatti per i programmi; l'output dei comandi porcelain può cambiare. Eccezione nella v0: la storia del file, da cui si ricava la base, si legge con `git log --raw`, e le modifiche nel working tree con `git diff --quiet`, entrambi porcelain; ciò che la configurazione dell'utente può cambiare è fissato sulla riga di comando (D-094).
- **I modi ammessi in un tree sono fissi** (`fsck` rifiuta gli altri): non si può introdurre un tipo "file condiviso" (T-02).
- **Si possono creare namespace di ref propri** (`refs/shared/*`), ma non viaggiano con clone, fetch e push ordinari (D-013, D-043).
- **`.git/config` e gli hook non sono versionati** e non passano con il clone (D-012, D-043).
- **Gli hook di commit non scattano sempre**: merge, rebase, cherry-pick e `--no-verify` li saltano (D-041).
- **Un conflitto si presenta con gli stage 1/2/3 nell'index** e i marker nel file (REQ-F-011).
- **L'index non si scrive a mano**: lo aggiornano `git update-index` e `git checkout-index` (REQ-NF-006).
- **`git foo` esegue `git-foo`** cercandolo nella exec-path di git e nel `PATH`: è il punto d'ingresso dell'estensione.
- **`git <cmd> --help` è intercettato da git** e cerca la pagina di manuale (REQ-F-019).

## 4. Glossario
| Termine | Significato |
|---|---|
| **File shared** | File tracciato da più repo, mantenuto con contenuto identico dall'estensione. |
| **ID** | Identificatore del file shared: 128 bit casuali (UUID v4) in esadecimale, generato al `set` (D-006). |
| **Nome** | Alias leggibile del file shared (default: nome del file). |
| **Peer** | Altra repo che usa lo stesso file shared, nota tramite il suo URL/path (D-012). |
| **Membership** | Elenco dei peer di un file shared: path locali in `.git/config`; URL remoti in `.gitshared` (fase 2). |
| **`.gitshared`** | File versionato alla radice della repo che dichiara i file shared (nome, ID, path, default dei branch attivi, URL dei peer remoti). Gestito solo dall'estensione (D-050). |
| **Versione shared** | Commit della catena `refs/shared/<id>`: il suo tree contiene il blob del file sotto un nome fisso, uguale in tutte le repo (D-042). |
| **H, I, T** | Blob del file shared in HEAD (H), nell'index (I) e nell'ultima versione di `refs/shared/<id>` (T). Lo stato si ricava dal loro confronto e dalla base (D-039, D-041, D-054). |
| **Base** | Versione shared da cui parte una modifica sul branch corrente. Non è salvata: si ricava dalla posizione nella storia del file delle versioni della catena (D-041, D-065). |
| **Pubblicare** | Creare una nuova versione shared e inviarla ai peer con `git push` di `refs/shared/<id>` (solo fast-forward, D-040). |
| **Ricevere** | Ottenere oggetti e ref di una versione da un peer: tramite il push del peer, oppure con `git fetch` durante `update`. Non tocca WT né index. |
| **Applicare** | Portare una versione ricevuta nel WT e nell'index (`git shared update`). |
| **Branch attivo** | Branch su cui un file shared è gestito come tale. L'elenco è locale, per file, fatto di nomi o pattern (D-055, D-056); nasce come copia del default in `.gitshared` (D-057). Sugli altri branch il file è **sospeso**: nessun controllo, avviso o pubblicazione. |
| **In attesa di commit** | `set`, `link` o `unlink` preparato ma non ancora confermato: diventa effettivo, anche per i peer, solo al commit (D-059). |
| **Seme** | Versione iniziale ricreata da `link` quando la catena non si trova (es. dopo un clone). È deterministica e viene sostituita dalla catena vera appena la si incontra (D-049, D-060). |
| **Divergenza** | Due versioni shared nessuna delle quali discende dall'altra. |
| **Padre** | Repo che ha dichiarato per prima il file shared (`set`). Nessun ruolo speciale dopo il `set` (D-004). |
| **WT, IDX, ODB, REF** | Working tree, index, object database, ref. |

## 5. Requisiti
> **BASELINE v2.35** (2026-11-17) — derivata dalle decisioni D-001…D-098. v2.35: variabili d'ambiente lette in UTF-8 (REQ-NF-003, D-097); suite di test solo con l'ultima git stabile (§7, D-098). v2.34: il blocco entra solo negli hook di shell; gli altri non si toccano (REQ-F-016, D-096). v2.33: un file non dichiarato sul branch corrente ma su un altro branch locale è *sospeso*, non *incoerente* (REQ-F-012, D-095). v2.32: lo stato non dipende dalla configurazione git dell'utente (REQ-F-012, D-094). v2.31: `git shared update --abort [<path>]` rinuncia all'unione in corso in un solo passo ed è la procedura di abbandono (REQ-F-009, REQ-F-011, D-093). v2.30: T avanza solo sulla versione ricevuta che la contiene; un'unione scaduta la toglie il primo comando che la vede; una versione già contenuta in T non si unisce di nuovo (REQ-F-009, REQ-F-011, REQ-F-012, D-092). v2.29: le versioni della catena sono tutte quelle raggiungibili da T, con entrambi i genitori delle unioni, per la base e per le modifiche locali (REQ-F-010, REQ-F-012, D-091). v2.28: compatibilità dichiarata chiusa; della matrice di D-032 resta non provata solo git 2.34 su Windows, ritenuta stabile (REQ-NF-004, D-090). v2.27: il modo di un hook che esiste già non cambia, solo l'hook creato da git shared riceve il bit di esecuzione (REQ-F-016, D-089); matrice delle versioni di git in §7 (D-032). v2.26: manuale anche in italiano (`help/it/`), comando `lang`, `-h` generale che spiega come aprire il manuale (REQ-F-018, REQ-F-019, REQ-NF-008, D-088). v2.25: aiuto distribuito, una pagina di testo per comando in `help/`; `git shared <comando> --help` (REQ-F-019, D-087). v2.24: `status --peers` (REQ-F-012, D-086). v2.23: un file sospeso perché nessun suo branch attivo esiste viene segnalato da `status`, hook, `update` e `publish`; `publish`/`update` dicono perché saltano un file (REQ-F-012, REQ-F-013, REQ-F-022, D-085). v2.22: `relocate` nella v0, con `--copy` e `--peer <vecchio> <nuovo>`; `shared.selfPath` e avviso di spostamento; `branch -a`/`-rm` (REQ-F-015, REQ-F-017, REQ-F-022, D-084). v2.21: comando `branch` nella v0, solo nomi esatti; l'elenco locale non può restare vuoto (REQ-F-022, D-083). v2.20: `prune` nella v0: toglie i peer spariti o usciti, mai quelli che adesso non rispondono, e le ref rimaste senza sezione (REQ-F-014, D-082). v2.19: `unlink` nella v0; conferma riconosciuta dal commit del comando; pulizia di una sezione locale orfana per ID (REQ-F-003, REQ-F-023, D-081). v2.18: ri-aggancio di `link` nella v0, anche dopo uno stash; `publish` non esegue il `pre-push` dell'utente (REQ-F-002, REQ-F-004, D-080). v2.17: un'attesa non si scarta se un branch locale la dichiara; `set` riprende un file dichiarato ma scollegato (REQ-F-001, REQ-F-023, D-079). v2.16: unione delle divergenze: la versione di merge nasce prima del commit, il conflitto vive nell'index, unico dato salvato `refs/shared-merge/<id>` (REQ-F-009, REQ-F-011, REQ-F-012, D-078). v2.15: le attese di `set`/`link` si completano da qualunque branch e si scartano solo sul branch del comando (REQ-F-023, D-077). v2.14: stato *da committare*; una versione applicata e non committata non è una modifica locale, una modifica in staging sì (REQ-F-010, REQ-F-012, D-076). v2.13: si pubblica solo con `publish`, `shared.autoPublish` e il `post-commit` eliminati (REQ-F-004, REQ-F-006, REQ-F-016, REQ-F-023, D-074); `-c`/`-m` committano solo il file shared e `.gitshared` (REQ-F-001, REQ-F-009, D-075). v2.12: in locale si pubblica con `publish`; pubblicazione al commit su richiesta (`shared.autoPublish`, default falso); niente `pre-commit` (REQ-F-004, REQ-F-006, REQ-F-016, REQ-F-023, D-073). v2.11: hook (M3): percorso rapido (D-069), una sola regola per `pre-commit` e `status` (REQ-F-006, D-070), ambiente degli hook (REQ-NF-006, D-071), `post-commit` con la T attuale (D-072). v2.10: base ricavata dalla posizione nella storia (D-065 rev. 1). v2.9: unico eseguibile (§6.2, D-067); in `.git/config` solo dati per repo (REQ-F-020, D-068). v2.8: base ricavata dalla prima corrispondenza nella storia (REQ-F-012, D-065); allocazione dinamica ammessa (REQ-NF-011, §8.3, D-066). v2.7: `link` su una destinazione già shared con un altro ID (REQ-F-002, D-064). v2.6: il `pre-commit` avvisa e non blocca (REQ-F-006, D-058); effetti di `set`/`link`/`unlink` al commit (REQ-F-023, D-059); seme sostituito dalla catena vera (D-060); `link` riusa la storia esistente (REQ-F-002, D-062); C non esclusivo (REQ-NF-001, D-063). v2.5: pattern e default versionato dei branch attivi (REQ-F-021, REQ-F-022, D-056, D-057). v2.4: branch attivi per file shared (REQ-F-022, D-055). v2.3: stato ricavato prima dalla base, poi dal confronto (REQ-F-012, D-054). v2.1: `.gitshared` al posto di `.gitattributes` (REQ-F-021). v2.2: nuovi limiti statici (REQ-NF-011). v1.1: documentazione utente (REQ-F-018, REQ-F-019). v2.0: revisione architetturale I-015 (trasporto git, stato ricavabile, regola H/I/T, rinomina e cancellazione, pubblicazione configurabile). Ogni modifica successiva passa dal registro decisioni (§9) e incrementa la versione.
> Formato: ogni requisito ha un ID univoco, un testo verificabile e l'origine. La verifica è tracciata in §7.

### 5.0 Il modello in sintesi
Da leggere prima dei requisiti. I termini sono definiti nel glossario (§4), le motivazioni in §9.

1. **Dichiarazione.** `git shared set <file>` rende shared un file della repo. Il file riceve un ID casuale ed è registrato in `.gitshared`, versionato alla radice della repo.
2. **Collegamento.** In un'altra repo, `git shared link <path-del-file>` collega lo stesso file. Al commit che conferma il collegamento le due repo diventano **peer** e ognuna registra l'altra (D-059). Tutte le repo sono alla pari. Se nella repo il file esiste già, `link` riusa la sua storia invece di sovrascriverlo (D-062).
3. **Versioni.** Ogni repo ha la ref `refs/shared/<id>`: una catena di commit che contiene solo il file shared. È la storia delle versioni del file, uguale in tutti i peer, separata dalla storia del progetto.
4. **Modifica e pubblicazione.** Il file si modifica e si committa come ogni altro. Quando l'utente lo decide, `git shared publish` crea una nuova versione nella catena (se la modifica parte dall'ultima versione) e la invia ai peer con `git push` della ref. Il commit non pubblica mai (D-074).
5. **Ricezione.** Nel peer cambia solo `refs/shared/<id>`. File, index e branch non sono toccati.
6. **Applicazione.** `git shared update` porta l'ultima versione nel working tree e nell'index; l'utente conferma con un commit.
7. **Protezione.** Si pubblica solo in fast-forward. Una modifica committata partendo da una versione vecchia risulta *divergente* e non viene pubblicata (D-058); due versioni indipendenti sono *divergenti* e `update` le unisce con un merge a 3 vie.
8. **Branch.** Il file è gestito come shared solo sui **branch attivi**, scelti dall'utente e salvati in locale; sugli altri è *sospeso* e si modifica come un file normale (D-055).
9. **Stato.** Per ogni file shared lo stato si ricava da tre blob: quello in HEAD (H), quello nell'index (I) e quello dell'ultima versione (T). Prima si ricava la base dalla storia del branch, poi si confronta H con T e con la base (D-054).

| Dato | Dove | Versionato | Contenuto |
|---|---|---|---|
| Dichiarazione | `.gitshared` | sì | nome, ID, path del file, default dei branch attivi; URL dei peer remoti (fase 2) |
| Membership locale | `.git/config`, sezione `[shared "<id>"]` | no | path dei peer locali, path della repo stessa, branch attivi (REQ-F-022) |
| Versioni | `refs/shared/<id>` | no (ref propria, replicata tra peer) | catena di commit con il blob del file |
| Hook | config o `.git/hooks/` | no | punti d'ingresso verso `git shared hook` |

### 5.1 Requisiti funzionali (REQ-F-xxx)
> Gli ID non sono in ordine numerico: i requisiti sono raggruppati per argomento e gli ID restano stabili.

**Dichiarazione e collegamento**
- **REQ-F-001 – `set`.** `git shared set <path>` dichiara shared un file tracciato della repo corrente. **Al comando**: genera l'ID (D-006); crea o aggiorna `.gitshared` con la sezione del file (nome, `id`, `path`, `branch`) e lo mette in staging (D-050). **Al commit di conferma** (REQ-F-023): crea `refs/shared/<id>` con la prima versione, presa dal blob del file in quel commit (D-002, D-013, D-042), e la sezione locale `[shared "<id>"]` in `.git/config` (D-012). Non committa, salvo `-c`/`--commit` o `-m <msg>` (D-007), che committano solo `.gitshared` (D-075). **File già dichiarato ma scollegato** (sezione in `.gitshared`, nessun dato locale: es. un'attesa scartata durante un `git stash`): `set` non rifiuta, riusa l'ID e il nome della sezione e registra di nuovo l'attesa; se la sezione è già committata la dichiarazione vale subito, senza commit (D-079). *Origine:* I-003, D-006, D-007, D-012, D-013, D-018.
- **REQ-F-021 – File `.gitshared`.** Ogni repo con file shared ha alla radice un file versionato `.gitshared`, in formato config di git, con una sezione per file shared: `[shared "<nome>"]` con `id`, `path` e zero o più `branch` (default dei branch attivi, D-057) in fase 1, più zero o più `url` di peer remoti (fase 2). Il file è letto e scritto solo dall'estensione, tramite `git config -f .gitshared`. Nessun path locale assoluto (D-003, D-050).
- **REQ-F-002 – `link`.** `git shared link <path-sorgente> [<path-destinazione>]` collega alla repo corrente un file shared di un'altra repo, indicato con il suo path nel WT della sorgente (D-005). Il tool risale a repo sorgente, path relativo e ID; ottiene la catena con `git fetch <sorgente> refs/shared/<id>` (D-040); scrive il file nel WT e nell'index a partire dal blob (D-042); aggiorna `.gitshared` e lo mette in staging (D-050); **al commit di conferma** (REQ-F-023) scrive in `.git/config` la membership completa (peer della sorgente + la sorgente) e si annuncia a tutti i peer, che lo aggiungono alla loro membership. *Origine:* D-002, D-005, D-012, D-040, D-064.
  - Path di destinazione: default = stesso path relativo della sorgente; path diverso ammesso come secondo argomento (D-028).
  - Se il file nella sorgente non è ancora shared → **errore** (D-019).
  - **Destinazione già esistente: si riusa la storia** (D-062). `link` non tocca il file e ricava la base come per ogni file shared (D-041, D-054):
    - il blob locale è quello dell'ultima versione → entra *allineato*;
    - il blob locale è quello di una versione precedente → entra *da applicare*: `update` lo porta all'ultima versione senza conflitti;
    - il blob locale non è nella catena, ma una versione della catena compare nella storia del file → entra *divergente* con una base vera: `update` fa un merge a 3 vie;
    - nessuna versione compare nella storia → nessuna base: errore; `-f` sovrascrive; `--merge` scrive il file con marker di conflitto (D-029).
  - **Ri-aggancio** (D-043, D-080): se la repo corrente ha già in `.gitshared` la sezione con lo stesso ID ma nessun dato locale (*scollegato*: dopo un clone, o un `link` scartato durante un `git stash`), `link` non dà errore: rifà il fetch, registra l'attesa e installa gli hook **senza toccare il file**, con nome e path della sezione locale (una destinazione diversa è un errore). Se la sezione è già committata il collegamento vale subito; altrimenti `.gitshared` torna in staging (con il file, se non è in HEAD) e vale al commit.
  - **Destinazione già shared con un altro ID** (D-064): se il path di destinazione è già dichiarato in `.gitshared` con un ID diverso da quello della sorgente, `link` dà **errore**. Con `--adopt` la repo adotta l'ID della sorgente: in un'unica operazione in attesa (REQ-F-023) la vecchia identità viene scollegata come con `unlink` (REQ-F-003) e il file viene collegato con il nuovo ID. Il file non viene toccato e la base si ricava come per una destinazione esistente (D-062); senza base vale D-029. Al commit i peer del vecchio ID ricevono la notifica di `unlink`, quelli del nuovo l'annuncio di `link`.
  - **Seme** (D-049, D-060): se la catena non si trova, `link` la ricrea con una versione seme deterministica. Quando più tardi la repo ottiene da un peer una catena che contiene una versione con lo stesso blob del seme, e la catena locale è fatta del solo seme, la ref locale viene **sostituita** con quella del peer. Il seme non porta informazione, quindi non si perde nulla. Se sul seme sono già state pubblicate versioni, o i blob non coincidono, vale la regola della divergenza (REQ-F-011).
- **REQ-F-003 – `unlink`.** `git shared unlink [-c | -m <msg>] <path>` toglie il file dalla gestione shared nella repo corrente. Il file resta nel WT come file normale. *Origine:* D-007, D-011, D-081.
  - **Al comando** (REQ-F-023): rimuove la sezione da `.gitshared`, e il file `.gitshared` se resta vuoto; mette in staging `.gitshared` (anche la sua cancellazione); registra l'attesa con il branch e il commit del comando (HEAD, che deve dichiarare l'ID). Non committa, salvo `-c`/`-m`, che committano solo `.gitshared` (D-075).
  - **Al commit di conferma**: i peer tolgono questa repo dalla loro membership (un peer irraggiungibile è un avviso, B-07); si cancellano `refs/shared/<id>` e le ref di lavoro; per ultima si toglie la sezione `[shared "<id>"]` da `.git/config`. È conferma una revisione (HEAD o un branch locale) che **discende dal commit del comando** e il cui `.gitshared` non dichiara più l'ID (D-081).
  - **Annullamento**: `git restore` di `.gitshared` sul branch del comando, come per `set` e `link`.
  - **File scollegato** (es. un clone, nessun dato locale): si toglie solo la sezione da `.gitshared`, senza attesa.
  - **Sezione locale orfana** (dati in `.git/config` senza dichiarazione: es. un `set -c` annullato con `git reset`): `unlink <id>` toglie subito i dati locali, se nessun branch locale dichiara l'ID; altrimenti indica su quale branch farlo.
  - Rifiuta un file con un'operazione già in attesa.
- **REQ-F-023 – Conferma al commit.** `set`, `link` e `unlink` agiscono in due momenti (D-059):
  - **Al comando** si tocca solo la repo corrente: working tree, index, `.gitshared` in staging, hook (REQ-F-016), oggetti ottenuti con il fetch e una **registrazione locale dell'operazione in attesa** in `.git/config` (tipo di operazione, ID, path del file, branch corrente e, per `link`, path della sorgente). Nessun'altra repo viene a sapere nulla.
  - **Al commit** che contiene la modifica a `.gitshared` l'operazione diventa effettiva: ref, sezione locale, membership, annuncio ai peer, elenco dei branch attivi. La completa il primo comando `git shared` successivo: nessun hook gira sul commit (D-074). Con `-c` o `-m` il comando committa e completa subito.
  - **Branch** (D-077). Il commit di conferma si cerca sul branch su cui è stato lanciato il comando, poi in HEAD: il completamento avviene da qualunque branch sia lanciato il primo comando `git shared`. Senza branch registrato (HEAD staccata al comando) vale solo HEAD.
  - **Annullamento.** Se prima del commit l'utente riporta `.gitshared` com'era (`git restore`), l'operazione in attesa viene scartata al primo comando `git shared` lanciato **su quel branch**: non resta traccia nelle altre repo. Prima di scartare si cerca l'ID nel `.gitshared` committato di **tutti i branch locali**: se uno lo dichiara, il commit di conferma è lì e l'operazione si completa da quel branch (D-079). Da un altro branch l'attesa resta com'è. Se il branch del comando non esiste più, l'attesa resta e un avviso dice come toglierla. Gli hook installati restano: senza file shared non fanno nulla.
  - `git shared status` mostra l'operazione come *in attesa di commit*, con il branch se è diverso da quello corrente.
  - Restano a effetto immediato i comandi che toccano solo dati locali non versionati: `branch --add/--remove`, `prune`, `relocate`.
- **REQ-F-020 – Rinomina e cancellazione.** `git shared mv <vecchio> <nuovo>` rinomina il file (`git mv`) e aggiorna `path` in `.gitshared`, unico posto in cui è scritto (D-068). La rinomina è locale: i peer non ne sono toccati (il nome nella catena è fisso, D-042). La cancellazione del file in una repo **non si propaga**: `status` la segnala come *incoerente* e propone `git shared unlink` (D-045).
- **REQ-F-022 – Branch attivi.** Per ogni file shared la repo tiene, in locale, l'elenco dei **branch attivi**: voci `branch` nella sezione `[shared "<id>"]` di `.git/config` (D-055). `git shared branch <file>` mostra l'elenco; `--add <branch>` e `--remove <branch>` lo modificano, con effetto immediato: è un dato solo locale e non versionato.
  - **Pattern** (D-056): una voce può essere un nome esatto (`main`) o un pattern con i jolly di git (`release/*`). Il confronto con il branch corrente è delegato a git (`git branch --list <pattern>`), non è implementato dall'estensione.
  - **Default versionato** (D-057): la sezione del file in `.gitshared` può contenere voci `branch`, che sono il default per chi collega la repo. `set` vi scrive il branch corrente. L'elenco locale nasce come **copia** del default al `set`, al `link` e al ri-aggancio dopo un clone (D-043); se il default manca, contiene il solo branch corrente. Dopo la copia vale solo l'elenco locale: una modifica successiva del default non lo cambia.
  - `git shared branch <file> --default --add/--remove <branch>` modifica il default in `.gitshared` e lo mette in staging; non committa, salvo `-c`/`-m`, che committano solo `.gitshared` (D-007, D-075). `git shared branch <file> --reset` ricopia il default nell'elenco locale (il branch corrente se il default è vuoto).
  - **v0** (D-083): `git shared branch [<file>]` mostra elenco locale e default, segnando il branch corrente e i nomi che non corrispondono a nessun branch (reportIdea B-21). Solo nomi esatti: un pattern è rifiutato con un messaggio. L'elenco locale **non può restare vuoto**: per non condividere più il file c'è `unlink`. `--add` di un branch che non esiste ancora è ammesso, con un avviso. Le modifiche locali richiedono un file collegato e senza attese; `--default` vale anche su un file scollegato.
  - Su un branch attivo valgono tutte le regole: controllo H/I/T, pubblicazione, avvisi.
  - Su ogni altro branch, e con HEAD staccata (checkout di un tag, bisect, rebase in corso), il file è **sospeso**: il `pre-commit` non controlla, il commit non pubblica, non ci sono avvisi. Il file si modifica come un file normale.
  - La ricezione non dipende dal branch: `refs/shared/<id>` è per repo (REQ-F-007).
  - `update` e `publish` lanciati a mano su un branch sospeso funzionano, con un avviso: sono atti espliciti dell'utente.
  - Quando le modifiche fatte su un branch sospeso arrivano con un merge su un branch attivo, lo stato si ricava come sempre (D-054): *da pubblicare* o *divergente*. La sospensione rimanda la pubblicazione, non scarta nulla.
- **REQ-F-016 – Installazione nella repo.** `set` e `link` installano nella repo gli hook necessari (`post-checkout`, `post-merge`, `post-rewrite`; nessun hook sui commit, D-073, D-074) **senza mai sovrascrivere hook esistenti**: hook da configurazione con git ≥ 2.54, altrimenti blocco delimitato da marker nei file di hook (D-030). Un hook esistente che non è uno script di shell (binario, Python, Perl, ...) non si tocca, con un avviso (D-096). Gli hook sono solo punti d'ingresso e comodità: la correttezza non dipende da loro (D-041).

**Pubblicazione e ricezione**
- **REQ-F-004 – Pubblicazione.** Per un file shared *da pubblicare* (modifica committata su base aggiornata, D-054), il sistema crea una nuova versione in `refs/shared/<id>` con genitore T e la invia a tutti i peer raggiungibili con `git push <peer> refs/shared/<id>` (D-016, D-040), senza eseguire il suo hook `pre-push` (`--no-verify`, D-080). Il push trasferisce anche gli oggetti. *Origine:* D-007, D-013, D-016, D-040, D-073.
  - **Quando** (D-073, D-074): solo su richiesta, con `git shared publish` (REQ-F-005). Il commit non pubblica mai.
- **REQ-F-005 – Pubblicazione manuale.** `git shared publish [<path>]` pubblica su richiesta ogni file in stato "da pubblicare" (hook non scattato, merge/pull/rebase, peer prima irraggiungibili, pubblicazione automatica disattivata). *Origine:* D-041.
- **REQ-F-006 – Solo fast-forward.** Una versione si pubblica solo se discende dall'ultima versione nota; il push verso un peer che ha una versione più recente o divergente è rifiutato da git (non fast-forward) e il sistema indica di eseguire `git shared update` (D-009, D-014, D-040).
  - **Nessun controllo prima del commit** (D-073; prima: regola H/I/T del `pre-commit`, D-039, D-058, D-070). Un commit non viene mai bloccato né rallentato. Dopo il commit lo stato si ricava con la regola di `status` (D-054, D-065):
    - *allineato* → si è applicato un update: niente da pubblicare;
    - *da pubblicare* → modifica su base aggiornata: la pubblica `publish`, con la T di quel momento;
    - *divergente* → modifica da una base obsoleta: non si pubblica; lo dicono `status` e `publish`. `git shared update` unirà le due versioni.
  - La protezione non viene da un blocco ma dalla regola fast-forward e dallo stato ricavato (D-041, D-054). Se gli hook non scattano, nulla viene perso: il file risulta "da pubblicare" o "divergente" in `status`.
- **REQ-F-007 – Ricezione.** Una repo riceve una versione in due modi: (a) dal push di un peer, accettato solo se fast-forward rispetto alla propria `refs/shared/<id>`; (b) con il fetch eseguito da `update` (REQ-F-009). In entrambi i casi arrivano ref **e oggetti**; WT e index non vengono toccati; funziona anche con modifiche non committate (D-015, D-040).
- **REQ-F-008 – Peer irraggiungibili.** Se un peer non è raggiungibile (path inesistente, permessi, lock), la pubblicazione prosegue con gli altri e il peer viene segnalato. *Origine:* I-004.

**Applicazione e conflitti**
- **REQ-F-009 – `update`.** `git shared update [<path>]`: (1) esegue `git fetch` di `refs/shared/<id>` da ogni peer raggiungibile, in ref temporanee (D-040); (2) se una versione ottenuta discende da quella locale, avanza `refs/shared/<id>` in fast-forward su quella, chiedendolo a git se ne restano più d'una (D-092); le versioni che non discendono da T, e da cui T non discende, restano da unire (REQ-F-011); (3) porta il branch a T: se è *da applicare* scrive T nel WT e nell'index a partire dal blob (`update-index --cacheinfo` + `checkout-index`, D-042), se è *divergente* unisce (REQ-F-011); (4) unisce una alla volta le versioni dei peer rimaste, saltando quelle che T contiene già (D-092). Non committa, salvo `-c`/`-m` (D-007, D-015), che committano solo i file applicati o uniti (D-075). `git shared update --abort [<path>]` rinuncia all'unione in corso (REQ-F-011, D-093).
- **REQ-F-010 – Modifiche locali.** Se il file shared ha modifiche non committate, `update` si rifiuta di sovrascriverle; `-f` / `--force` sovrascrive (D-022). È una modifica locale un working tree diverso dall'index, oppure un index diverso da HEAD e da ogni versione della catena (tutte quelle raggiungibili da T, D-091): sono protette anche le modifiche in staging. Una versione applicata da un `update` precedente e non ancora committata non è una modifica locale: `update` la sostituisce con l'ultima (D-076).
- **REQ-F-011 – Divergenza.** Sono divergenti due versioni nessuna delle quali discende dall'altra (D-014): due peer hanno pubblicato senza vedersi, oppure il branch corrente ha modificato il file partendo da una base obsoleta (D-041). `update` le unisce con un merge a 3 vie del contenuto (D-078):
  - **Divergenza del branch**: base = blob della base ricavata (D-065), nostra = H, loro = blob di T. La versione di merge ha **un genitore**, T.
  - **Divergenza tra versioni** (T e la versione P di un peer): base = blob del merge-base delle due, nostra = contenuto corrente del file, loro = blob di P. La versione di merge ha **due genitori**, T (primo) e P.
  - **Senza base** (nessuna versione nella storia del branch, D-029): base vuota.
  - **La versione di merge nasce prima del commit.** Un'unione pulita scrive il risultato nel WT e nell'index, crea subito la versione nella catena e avanza `refs/shared/<id>` (condizionato a T, REQ-NF-005): il file è *da committare* (D-076). Se il risultato è uguale a T non nasce nessuna versione (è un'applicazione); se è uguale a H non c'è nulla da committare. Dopo il commit si invia con `publish`.
  - **Ordine**: prima il branch verso T, poi le versioni dei peer, una alla volta. Le unioni pulite proseguono; al primo conflitto `update` si ferma: al più un conflitto per comando.
  - **Conflitto**: presentato come in `git merge` (stage 1/2/3 nell'index, marker `ours`/`theirs` nel file, §3). T non cambia. L'unione in corso è registrata in `refs/shared-merge/<id>` (§6.3), unico dato salvato; vale solo finché il path è in conflitto nell'index o l'index è diverso da HEAD.
  - **Conclusione**: l'utente corregge il file, fa `git add` e rilancia `git shared update` (con `-c` committa). Se nel frattempo T è avanzata, la risoluzione viene unita anche alla nuova T, con base la T di partenza.
  - **Abbandono** (D-093): `git shared update --abort [<path>]`. In un solo comando il file torna quello di HEAD nell'index (gli stage di conflitto spariscono) e nel WT, e la registrazione si toglie; nessun fetch, T non cambia. Senza `<path>` vale per ogni file con un'unione registrata; senza nessuna unione è un errore (codice 1). Non si combina con `-c`, `-m`, `-f` (uso, codice 129). È la procedura che indicano il messaggio del conflitto e il manuale.
  - **Abbandono con `git restore -s HEAD -SW -- <file>`**: tollerato. La registrazione scade (index uguale a HEAD, nessuno stage) e la toglie il primo comando `git shared` che ricava lo stato (`status`, `publish`, `update`, gli hook di avviso), D-092. Una modifica messa in staging prima di quel comando varrebbe come risoluzione, come con `MERGE_HEAD` in git.
  - **Modifiche locali** (REQ-F-010): un'unione non parte se il file ha modifiche non committate, salvo `-f`.

**Stato e manutenzione**
- **REQ-F-012 – `status`.** `git shared status` elenca per ogni file shared: nome, path, ID e stato. Lo stato è **ricavato** ogni volta da H, I, T e dalla catena, senza dati salvati (D-041), in due passi (D-054): **prima si ricava la base B** (D-041, D-065), **poi** si confronta H con T e con il blob di B. Le versioni sono tutte quelle raggiungibili da T, con entrambi i genitori delle unioni (D-091): la versione di un peer unita da un'altra repo è una base anche per quel peer, che risulta *da applicare* e non *divergente*. Lo stato non dipende dalla configurazione git dell'utente (`log.showRoot`, `log.follow`, `diff.autoRefreshIndex`…, D-094). Il solo fatto che H coincida con il blob di una versione della catena non decide lo stato: lo stesso contenuto può comparire in più punti della storia (es. un revert).
  - *allineato*: H = T;
  - *da committare*: H ≠ T e I = T: l'ultima versione è già nell'index (un `update` senza commit); resta solo il commit (D-076). Si controlla prima della base;
  - *unione in corso*: `refs/shared-merge/<id>` esiste e il path è in conflitto nell'index, oppure la risoluzione è in staging (I ≠ H): si rilancia `update` (D-078). Si controlla prima di ogni altro stato ricavato; se la ref c'è ma è scaduta, la si toglie (D-092);
  - *da applicare*: B è più vecchia di T e H = blob di B (il branch è rimasto indietro, senza modifiche proprie);
  - *da pubblicare*: B = T e H ≠ T (modifica su base aggiornata; vale anche se H coincide con il blob di una versione precedente, come dopo un revert);
  - *divergente*: B è più vecchia di T e H ≠ blob di B; oppure nessuna versione della catena compare nella storia del branch (nessuna base, caso D-029); oppure i genitori di un merge danno basi diverse (D-065);
  - *in attesa di commit*: `set`, `link` o `unlink` preparato ma non ancora confermato da un commit (REQ-F-023);
  - *sospeso*: il branch corrente non è tra i branch attivi del file, oppure HEAD è staccata (REQ-F-022); accanto è mostrato comunque lo stato ricavato; se **nessuno** dei branch attivi esiste più (es. dopo una rinomina), una riga `note` lo dice con il comando che lo risolve (D-085). È *sospeso* anche un file che il `.gitshared` di questo branch non dichiara mentre un altro branch locale sì (branch nato prima del `set`): stato `not declared here`, con path e branch presi da quel branch in una riga `note` (D-095);
  - *scollegato*: sezione in `.gitshared` senza sezione in `.git/config` (es. dopo un clone): suggerisce `link` (D-048);
  - *incoerente*: file rinominato o cancellato (REQ-F-020); oppure sezione in `.git/config` senza sezione nel `.gitshared` del working tree, quando HEAD la dichiara (`.gitshared` toccato a mano) o nessun branch locale la dichiara (sezione orfana, `unlink <id>`) (D-095).
  - Con `--peers` mostra anche, per ogni peer: *stessa versione*; *versione più vecchia* (gli manca l'ultima di questa repo: `publish`); *versione più nuova o diversa* (ha qualcosa che qui non c'è: `update`); *nessuna versione* (`publish`); *non condivide più il file* o *non trovato* (`prune`); *illeggibile adesso*. Nessun fetch: si legge la sua `refs/shared/<id>` (D-086).
- **REQ-F-013 – Avviso al checkout.** Dopo un checkout, un merge o una riscrittura (hook `post-checkout`, `post-merge`, `post-rewrite`), se un file shared non è *allineato* il sistema avvisa con lo stato e il comando da lanciare, senza modificare nulla (D-010, D-041). Nessun avviso per i file sospesi sul branch di arrivo (REQ-F-022), salvo quando nessuno dei loro branch attivi esiste più: allora l'avviso dice come aggiungerne uno (D-085).
- **REQ-F-014 – Peer mancanti.** `git shared prune [-n] [<path>]` toglie dalla membership dei file shared collegati (o di quello indicato) i peer che non ci sono più, qui e in tutti i peer che restano (D-082). Effetto immediato: sono dati locali (REQ-F-023).
  - **Sparito**: al path del peer non c'è più una repo (cartella o `.git` assenti). Si toglie.
  - **Uscito**: la repo c'è ma non ha più la sezione `[shared "<id>"]` (es. un `unlink` mentre questa repo era irraggiungibile, reportIdea B-07). Si toglie.
  - **Irraggiungibile**: la repo c'è ma git non riesce a leggerla (lock, permessi, configurazione rotta). **Resta**, con un avviso: `prune` non toglie mai ciò che adesso non risponde.
  - Senza `<path>` toglie anche le ref di lavoro rimaste senza sezione locale: `refs/shared/<id>`, `refs/shared-incoming/<id>/*`, `refs/shared-merge/<id>` (reportIdea B-27, C-22).
  - `-n` / `--dry-run` mostra solo cosa toglierebbe. Un `set` o un `link` in attesa non si tocca.
- **REQ-F-015 – Repo spostata o copiata.** `git shared relocate [<vecchio-path>]` comunica ai peer il nuovo path della repo corrente: ogni peer sostituisce la voce del vecchio path (o la aggiunge, se l'aveva tolta con `prune`); senza argomento vale `shared.selfPath` (D-023, D-035, D-084).
  - **Copia** (reportIdea B-22): se al vecchio path c'è ancora una repo, `relocate` rifiuta e suggerisce `--copy`. `git shared relocate --copy [<vecchio-path>]` fa della copia un peer in più: la repo originale entra tra i suoi peer (se condivide ancora il file) e tutti i peer conoscono la copia. Con `--copy` e nessuna repo al vecchio path, rifiuta.
  - **Peer spostato**: `git shared relocate --peer <vecchio-path> <nuovo-path>` sostituisce qui la voce del peer, se al nuovo path c'è una repo che condivide il file e al vecchio non c'è più nulla. Serve quando il peer spostato non ha lanciato `relocate`, o non raggiungeva questa repo.
  - Ripetibile: nessun peer doppio. Effetto immediato (REQ-F-023).
- **REQ-F-017 – Rilevamento spostamento.** Ogni repo con almeno un file collegato registra il proprio path (`shared.selfPath`, scritto al primo comando `git shared` che non lo trova); a ogni comando, se la posizione attuale è diversa, il sistema avvisa e suggerisce `relocate` (o `relocate --copy`). Nessun avviso in un worktree secondario, che condivide config e ref con la repo principale (D-084). *Fuori dalla v0:* il suggerimento analogo per un peer irraggiungibile durante `publish` o `status` (D-035).

**Documentazione utente**
- **REQ-F-018 – Aiuto rapido `-h`.** `git shared -h` stampa la sintassi sintetica di tutti i sottocomandi; `git shared <sottocomando> -h` quella del singolo sottocomando (opzioni, argomenti). Uscita con codice 129, come i comandi git. Testo nella lingua configurata (D-025). *Origine:* D-038.
- **REQ-F-019 – Pagine di aiuto.** L'aiuto è **distribuito**: una pagina di testo semplice per comando in `help/` (`help/<comando>.txt`), più una pagina generale (`help/git-shared.txt`) breve, che elenca i comandi e rimanda alle pagine dei singoli comandi (D-087). *Origine:* D-038, D-087.
  - `git shared <comando> --help` e `git shared help <comando>` stampano la pagina del comando; `git shared help` e `git-shared --help` la pagina generale. Le pagine sono incorporate nell'eseguibile al build: ci sono sempre, anche se l'eseguibile viene spostato.
  - Vincolo tecnico: git intercetta solo `git shared --help` (non `git shared <comando> --help`, provato) e lo trasforma in `git help shared`, che cerca la pagina `git-shared`: HTML nella cartella della documentazione di git su Windows, man page su Linux. `make doc` le genera dalla stessa `help/git-shared.txt`; `make install` le installa (man in `~/.local/share/man/man1`; HTML in `git --html-path`, che su Windows richiede i permessi di amministratore: se mancano, un messaggio dice cosa copiare).
  - Formato: testo semplice, nessun linguaggio di markup né strumento esterno. Lo strumento di build `tools/embed.c` (C, nessuna dipendenza) produce sorgente incorporato, HTML (`<pre>`) e man page (REQ-NF-002). Lingue: inglese (`help/`) e italiano (`help/it/`), scelte con `shared.lang`; una pagina senza traduzione si mostra in inglese (D-088). La pagina di `git shared --help` si installa nella lingua scelta al momento di `make install`.
  - `git shared -h` elenca i comandi e spiega come aprire il manuale (`help`, `--help`, `<comando> --help` con un esempio) e come averlo in italiano (`git shared lang it`).
  - `git shared <comando> -h` resta l'uso breve (REQ-F-018).

### 5.2 Requisiti non funzionali (REQ-NF-xxx)
- **REQ-NF-001 – Linguaggio.** Il nucleo è in **C11** (sottoinsieme D-031), stile §8. Il C non è una scelta assoluta: parti del sistema possono essere script (`.sh`, `.bat`). La ripartizione tra C e script si decide in architettura (§6, D-063).
- **REQ-NF-002 – Dipendenze.** Nessuna dipendenza di runtime oltre a git e alla libreria C standard / API di sistema (D-017). Gli eventuali script usano solo interpreti già presenti con git o con il sistema operativo (D-063).
- **REQ-NF-003 – Piattaforme.** Linux e Windows (Git for Windows). macOS fuori scope in fase 1 (D-024).
- **REQ-NF-004 – Versione di git.** Versione minima supportata: **git 2.34** (D-032).
- **REQ-NF-005 – Atomicità.** Ogni aggiornamento di `refs/shared/<id>` è atomico e condizionato: `git update-ref <ref> <nuovo> <vecchio>` in locale, regola fast-forward di `git push` verso i peer. Un errore a metà operazione non lascia stato incoerente nella repo locale; un push fallito verso un peer lascia quel peer semplicemente indietro.
- **REQ-NF-006 – Index.** Il sistema non scrive mai direttamente `.git/index` né altri file interni di git: usa solo comandi plumbing (§3). Negli hook l'index è quello indicato da `GIT_INDEX_FILE`; i comandi diretti a un'altra repo partono senza le variabili d'ambiente che legano git alla repo corrente (D-071).
- **REQ-NF-007 – Prestazioni e memoria.** Prestazioni e occupazione di memoria sono obiettivi primari. Soglie (D-036): `status` solo locale < 100 ms su Linux e < 300 ms su Windows; pubblicazione verso 32 peer < 1 s; memoria del processo (statica + stack + heap, D-066) < 1 MiB.
- **REQ-NF-008 – Interfaccia.** Sintassi e convenzioni come i comandi git (opzioni brevi/lunghe, `-h`, codici di uscita: 0 successo, ≠0 errore). Messaggi in inglese di default, italiano selezionabile (D-025): `git shared lang <en|it>`, che scrive `shared.lang` in `git config --global`. **v0** (D-088): `git shared lang [en|it]` mostra o scrive `shared.lang`; la lingua sceglie le pagine del manuale. I messaggi dei comandi sono ancora solo in inglese.
- **REQ-NF-009 – Hash.** Supporto repo SHA-1 e SHA-256 (D-033). Tutti i peer di un file shared devono usare lo stesso algoritmo; `link` tra algoritmi diversi → errore.
- **REQ-NF-010 – Contenuto.** Solo file di testo in fase 1 (D-026).
- **REQ-NF-011 – Memoria e limiti.** Allocazione dinamica ammessa (§8.3, D-066): un'allocazione fallita termina il comando con un messaggio di errore, mai con un comportamento indefinito. Limiti (D-034, da rivedere in architettura): path e URL ≤ **4096** caratteri; ≤ **32 peer** per file shared; **nessun limite** al numero di file shared per repo né al numero di versioni della catena. Buffer di riga per l'output dei comandi git: limite path + 256, verificato a compile-time (D-037). Il superamento di un limite produce un errore esplicito, mai un troncamento silenzioso. Il **contenuto** dei file shared non passa mai dai buffer dell'estensione: viaggia come blob tra comandi git (D-042). Tutti i limiti sono costanti definite in un unico modulo.
- **REQ-NF-012 – Confronti sui blob.** Ogni confronto di contenuto si fa tra oid di blob (H, I, T), mai tra byte dei file nel WT: le conversioni di fine riga (`core.autocrlf`, attributo `eol`) possono differire tra repo (D-046).

### 5.3 Vincoli
- Solo repo locali sulla stessa macchina (D-001).
- Nessuno store o servizio esterno alle repo (D-012).
- File vero nel WT, non puntatore (D-018). Unico file versionato aggiunto dall'estensione: `.gitshared` (D-050).
- Accesso a git solo via comandi git: plumbing, più `fetch` e `push` per il trasporto (D-017, D-040).

### 5.4 Fuori scope (fase 1)
- **Fase 2 – remoto** (decisa nelle linee principali, non ancora tradotta in requisiti): peer come URL in `.gitshared` (D-050), broadcast della sola catena (D-051), pubblicazione al `git push` (D-052), ordine fisso di push (D-053), ricollegamento dopo il clone (D-048, D-049, D-060), pubblicazione solo dai branch attivi e `publish` eseguibile dalla CI dopo un merge fatto sul server (D-061).
- Variante puntatore + clean/smudge (D-018).
- Propagazione ad anello (D-016).
- Directory e glob (D-027).
- File binari (D-026).
- macOS (D-024).

## 6. Architettura SW
> Descrive la v0 com'è nel codice: perimetro e avanzamento (§6.0), contesto (§6.1), moduli (§6.2), formati (§6.3), flussi (§6.4), errori (§6.5).

### 6.0 Perimetro della v0
La v0 è il **core solo locale**: tutto ciò che serve perché due o più repo sulla stessa macchina condividano un file, senza i casi rari. Si costruisce in quattro passi, ognuno provabile da solo.

| Passo | Contenuto | Requisiti |
|---|---|---|
| **M0 – Scheletro** | Moduli di base (§6.2); `git shared -h`, `git shared help`; `status` che elenca le sezioni di `.gitshared` | REQ-F-018, REQ-NF-008 |
| **M1 – Una repo** | `set` (con `-c`/`-m`), creazione della catena, `status` con *allineato*, *da pubblicare*, *in attesa di commit* | REQ-F-001, REQ-F-021, REQ-F-023, REQ-F-012 (parte) |
| **M2 – Due repo** | `link` su destinazione nuova, `publish`, `update` in fast-forward, `status` senza merge | REQ-F-002 (parte), REQ-F-004, REQ-F-005, REQ-F-007, REQ-F-009, REQ-F-010 |
| **M3 – Hook** | Installazione con marker; hook di avviso dopo checkout, merge e riscrittura; nessun hook sui commit (D-074) | REQ-F-013, REQ-F-016 |

> **Stato (2026-11-15): M0, M1 e M2 completati.** `make` compila senza warning (`-Wall -Wextra -Wpedantic -Wshadow`), `make test` passa 185 test (§7). Tempi medi su Windows (soglia di REQ-NF-007: `status` 300 ms, pubblicazione verso 32 peer 1 s): `status` con 5 file allineati 81 ms; con un file da pubblicare 93 ms; con un file con 30 commit di storia 118 ms; `set` 157 ms; `publish` verso 8 peer 386 ms; `update` da 8 peer 293 ms. Da misurare: `publish` verso 32 peer. I file `*_posix.c` non sono ancora stati compilati né provati su Linux.
>
> Limiti noti di M2: `link` richiede che la destinazione non esista (D-062 e D-064 fuori dalla v0).
>
> **Unione delle divergenze (D-078, 2026-11-16).** `update` unisce le divergenze del branch e quelle tra versioni dei peer (REQ-F-011); `publish` crea la versione dalla T usata per ricavare lo stato (reportIdea B-31). 386 test. Limiti: una risoluzione identica al contenuto locale vale come abbandono; un commit della risoluzione fatto prima di rilanciare `update` riporta a *divergente* (l'unione si rifà); il modo del file resta `100644`.
>
> **Attese più robuste (D-079, 2026-11-16).** Prima di scartare un'attesa si cercano i branch locali che la dichiarano (reportIdea B-39); `set` riprende con lo stesso ID un file dichiarato ma scollegato (B-40). 407 test. Limite: `set` su un file scollegato dopo un **clone** crea una catena nuova, non collegata a quella dei peer: lì va usato `link` (D-080).
>
> **Ri-aggancio e push senza hook (D-080, 2026-11-16).** `link` ricollega un file *scollegato*: dopo un clone, o dopo un `link` scartato da uno stash. `publish` fa il push con `--no-verify` (reportIdea C-20). 439 test.
>
> **`unlink` (D-081, 2026-11-17).** Primo comando di manutenzione: toglie un file dalla condivisione, al commit i peer dimenticano la repo. 504 test.
>
> **`prune` (D-082, 2026-11-17).** Toglie i peer spariti o usciti, qui e negli altri peer, e le ref rimaste senza sezione. 546 test.
>
> **`branch` (D-083, 2026-11-17).** Mostra e cambia i branch attivi e il loro default; segna i nomi che non esistono più. 616 test.
>
> **`relocate` (D-084, 2026-11-17).** Repo spostate (`relocate`), copiate (`--copy`) e peer spostati (`--peer`); ogni comando avvisa se la repo non è dove ricorda. Con `relocate` i comandi di manutenzione della v0 sono completi. 670 test.
>
> **Nessun silenzio (D-085, 2026-11-17).** File sospeso perché nessun suo branch esiste più: nota in `status`, avviso negli hook, in `update` e `publish`. `publish`/`update` su un file in attesa o scollegato: errore con il motivo se indicato, una riga se saltato. 696 test.
>
> **`status --peers` (D-086, 2026-11-17).** Per ogni peer: se c'è, se condivide ancora il file e com'è la sua versione rispetto a quella di questa repo. 718 test.
>
> **Aiuto distribuito (D-087, 2026-11-17).** `help/`, una pagina per comando; `git shared <comando> --help`; `make doc`/`make install` per `git shared --help`. 760 test.
>
> **Manuale in italiano (D-088, 2026-11-17).** `help/it/`, comando `lang`, `-h` che spiega dove sta il manuale. 823 test.
>
> **Linux e modo degli hook (D-089, 2026-11-17).** Compilato e provato in WSL2 (Ubuntu 22.04, GCC 11) con git 2.34 (il minimo) e 2.56.1 (l'ultima stabile): nessun warning, tutti i test passano con entrambe. Su Windows passano anche con l'ultima stabile (PortableGit 2.56.1). Un hook dell'utente tiene il suo modo. 832 test (5 saltati su Windows, che non ha il bit di esecuzione).
>
> **Compatibilità chiusa (D-090, 2026-11-17).** Provati Windows con git 2.43 e 2.56.1, Linux con 2.34 e 2.56.1. Non provata git 2.34 su Windows, ritenuta stabile (§7).
>
> **Git in un path non ASCII (D-097, 2026-11-17).** Chiuso C-05: `GIT_EXEC_PATH` si legge in UTF-8. Da qui la suite gira solo con git 2.56.1 (D-098): 935 test su Windows (PortableGit) e su Linux (WSL2).
>
> **Hook non di shell (D-096, 2026-11-17).** Chiuso C-02: il blocco entra solo negli hook di shell; un hook Python, Perl o binario non si tocca, e uno già rovinato si ripara. 931 test su Windows (git 2.43) e su Linux (WSL2, git 2.34 e 2.56.1).
>
> **Branch nati prima del `set` (D-095, 2026-11-17).** Chiuso B-16: lì il file è *sospeso* (`not declared here`) con il branch su cui è shared, non *incoerente*. 916 test su Windows (git 2.43) e su Linux (WSL2, git 2.34 e 2.56.1).
>
> **Stato indipendente dalla configurazione git (D-094, 2026-11-17).** Chiuso B-32: `log.showRoot`, `log.follow` e `diff.autoRefreshIndex` cambiavano lo stato. 904 test su Windows (git 2.43) e su Linux (WSL2, git 2.34 e 2.56.1).
>
> **`update --abort` (D-093, 2026-11-17).** Rinuncia all'unione in corso in un solo comando: è la procedura di abbandono, e chiude B-48 anche nel caso che D-092 lasciava scoperto. 895 test su Windows (git 2.43) e su Linux (WSL2, git 2.34 e 2.56.1).
>
> **Unioni tra versioni (D-092, 2026-11-17).** Chiusi B-47 (T avanzava sulla prima versione ricevuta invece che su quella che la contiene), B-48 (un'unione abbandonata tornava viva con la prima modifica in staging) e B-49 (versione di unione doppia). 873 test su Windows (git 2.43) e su Linux (WSL2, git 2.34 e 2.56.1).
>
> **Catena letta come grafo (D-091, 2026-11-17).** Chiuso B-46, l'unico bug grave aperto: la versione di un peer unita da un'altra repo è riconosciuta come versione, e il suo `update` applica invece di unire con una base sbagliata. 845 test su Windows (git 2.43) e su Linux (WSL2, git 2.34 e 2.56.1).
>
> **M3 completato (rev. D-074, 2026-11-16).** `set`/`link` installano solo `post-checkout`/`post-merge`/`post-rewrite`, che avvisano dei file non allineati: **il commit non paga nulla** e si pubblica solo con `publish`. `shared.autoPublish` e il `post-commit` sono eliminati; `hook install` toglie i blocchi `pre-commit` e `post-commit` delle versioni precedenti. Stato *da committare* (D-076); attese completate da qualunque branch (D-077). 308 test.
>
> Costo di un commit su Windows (media di 20, misurato con D-073): senza hook 67–68 ms; con gli hook di git shared 66–77 ms (uguale, entro il rumore). Nella prima versione di M3 (`pre-commit` + `post-commit` sempre) un commit qualunque costava 164–168 ms.
>
> Limiti noti di M1: la regola dei merge di D-065 (basi diverse dai due genitori → *divergente*) non è applicata; la catena si legge seguendo il primo genitore (le versioni di merge arrivano con M2); sul branch nato prima del `set` lo stato è *incoerente* (A-21).

**Fuori dalla v0**, in ordine di probabile arrivo: `link` su destinazione esistente e `--adopt` (D-062, D-064); `mv`; pattern dei branch attivi (D-056: nella v0 solo nomi esatti); seme (catena non trovata al `link`); messaggi dei comandi in italiano; hook da configurazione (git ≥ 2.54); prove con SHA-256 (il formato degli oid è già previsto).

### 6.1 Vista di contesto
> Fatti verificati con il prototipo `hello/`, poi eliminato (git 2.43, Windows, MinGW-w64 GCC 13.2). Su Linux, con git 2.34 e 2.56.1, li confermano i test di sistema (2026-11-17).

```
utente ──> git ──(lancia git-shared)──> git-shared ──(processi figli)──> git: plumbing, fetch, push
                                            │
                                            └──(git push, git -C <peer> config)──> peer locali
```

- **Punto d'ingresso.** `git shared <args>` → git cerca un eseguibile `git-shared` prima nella exec-path di git (`git --exec-path`), poi nel `PATH`, e lo lancia con `argv[1..]` = `<args>`. Su Windows va bene `git-shared.exe` oppure uno script senza estensione con la riga `#!`. Basta il file: nessuna registrazione né configurazione. `git help -a` lo elenca tra gli "External commands".
- **Installazione.** Copiare `git-shared` in una cartella del `PATH` dell'utente (nessun permesso di amministratore) oppure nella exec-path di git (servono i permessi sulla cartella di git). Su Linux serve il bit di esecuzione.
- **Ambiente.** git imposta `GIT_EXEC_PATH`; non imposta `GIT_DIR` né `GIT_PREFIX` e non cambia la directory corrente. Il comando parte dalla cartella dell'utente, anche fuori da una repo: trovare la repo è compito dell'estensione (`git rev-parse --show-toplevel`, `--git-dir`). Negli hook è diverso: vedi A-27.
- **`--help`.** `git shared --help` diventa `git help shared`: su Windows cerca `git-shared.html` in `git --html-path` e, se manca, esce con `fatal: ... documentation file not found` (codice 128); su Linux cerca la man page. Conferma REQ-F-019. `git-shared --help` lanciato direttamente arriva invece all'eseguibile.
- **Linguaggio indifferente, shell dell'utente irrilevante.** Lo stesso comando funziona come eseguibile C e come script `sh`. È git a lanciare l'estensione, non la shell in cui l'utente scrive: lo script `sh` parte anche quando git è lanciato da PowerShell. Git invece **non trova** `git-<nome>.bat` né `git-<nome>.cmd`: un file batch non può fare da punto d'ingresso.
- **Costo dell'avvio** (media di 20 lanci, Windows): `git --version` 30 ms; `git shared -h` con l'eseguibile C 45 ms; con uno script `sh` 45 ms; con uno script `sh` che lancia l'eseguibile 52 ms. Il grosso è l'avvio di git; ogni processo in più costa pochi ms, ma una pubblicazione verso 32 peer lancia decine di processi.
- **Accesso a git.** Il prototipo lancia git con `popen` (`_popen` su Windows, che passa da `cmd.exe`). Funziona (D-017), ma la shell intermedia impone regole di quoting diverse per piattaforma, e i path con spazi sono normali (questo progetto ne ha). In architettura serve un modulo che lanci i processi senza shell (`CreateProcess` su Windows, `fork` + `exec` su Linux).

### 6.2 Decomposizione in moduli

#### Punto d'ingresso (D-067)
Un **unico eseguibile** `git-shared` (`git-shared.exe` su Windows) con una tabella interna dei sottocomandi, come fa git per i comandi interni.
- Un dispatcher `.bat` non è possibile: git non lo trova (§6.1).
- Un dispatcher `sh` che lancia un eseguibile per sottocomando funziona su entrambe le piattaforme e permetterebbe sottocomandi in script (A-18). Costa un processo in più a ogni chiamata (~7 ms misurati, §6.1), anche a ogni commit tramite gli hook. In cambio porta più file da installare e più pezzi da tenere coerenti (opzioni, messaggi, codici di uscita).
- Le differenze tra Windows e Linux non stanno nel modo in cui git lancia l'estensione, che è identico, ma nel modo in cui l'estensione lancia git (§6.1, accesso a git). Si risolvono dentro il programma, con un modulo `proc` che ha due implementazioni.
- Il dispatcher `sh` resta possibile in seguito, senza cambiare nulla per l'utente.

#### Moduli
Ogni livello usa solo quelli sotto. Solo `proc` e `sys` contengono codice diverso per piattaforma: i file `*_win.c` e `*_posix.c`, scelti dal Makefile.

I moduli di base non legati a git shared (`proc`, `sys`, `str`, `mem`, `err`) stanno in `utils/`, il core in `src/`. `utils/` non include nulla da `src/`: si potrebbe riusare in un altro progetto così com'è.

| Livello | Modulo | Compito |
|---|---|---|
| Ingresso | `main` | Opzioni globali, tabella dei sottocomandi, codici di uscita |
| Comandi | `cmd_*` | Un file per sottocomando: `set`, `link`, `unlink`, `status`, `update`, `publish`, `prune`, `branch`, `relocate`, `lang`, `hook`, `help` |
| | `cmd_common` | Parti comuni: opzioni `-c`/`-m`/`-f`, apertura della repo, commit dedicato, scrittura di un blob nel file, scelta dei file su cui lavorare |
| Modello | `shared` | La repo (`Repo_t`: radice, branch, voci) e i file shared (`.gitshared` + sezioni locali); completamento delle operazioni in attesa (REQ-F-023) |
| | `state` | Derivazione dello stato e della base (D-054, D-065) |
| | `chain` | Catena `refs/shared/<id>`: creazione di una versione, blob delle versioni |
| | `merge` | Unione a 3 vie nell'index con il solo plumbing; ref dell'unione in corso (D-078) |
| | `peers` | Membership, annuncio, push e fetch verso i peer: **in parallelo**, il tempo è quello del peer più lento. Verso un peer sempre `--git-dir=<peer>/.git`, così un peer cancellato non porta a una repo che lo contiene |
| | `hooks` | Installazione dei blocchi con marker; la logica degli hook è in `cmd_hook` (`git shared hook <evento>`, comando interno) |
| Accesso a git | `git` | Lancia comandi git; oid in binario (`Oid_t`, 33 byte); dialogo con `cat-file --batch-check` (`GitBatch_t`) |
| Piattaforma | `proc` | Processi **senza shell**, con stdin e stdout in pipe: `proc_win.c` (`CreateProcess`), `proc_posix.c` (`fork` + `exec`) |
| | `sys` | Argomenti e variabili d'ambiente in UTF-8 (`_wgetenv` su Windows, D-097), numeri casuali (`BCryptGenRandom`, `/dev/urandom`), file con path UTF-8 |
| | `path` | Path come stringhe: assoluto o relativo, cartella e nome, unione con `.` e `..` risolti. I path delle repo non si normalizzano: si salvano come li restituisce `rev-parse --show-toplevel`, che su Windows dà sempre la stessa forma (reportIdea §5, A-26) |
| Base | `str`, `mem` | Stringhe a lunghezza variabile, `xmalloc` (D-066) |
| | `bounds`, `err` | Limiti (D-034); codici di errore interni e codici di uscita |
| | `msg` | Tabelle dei messaggi (§8.5) |

#### Alberatura
```
git-shared-file/
├── project.md, breef.md, reportLocal.md
├── reportIdea.md             solo locale (in .gitignore)
├── Makefile                  build Linux e Windows (MinGW)
├── src/                      il core
│   ├── main.c
│   ├── cmd.h                 prototipi dei sottocomandi
│   ├── cmd_common.c/.h       parti comuni ai sottocomandi
│   ├── cmd_set.c  cmd_link.c  cmd_unlink.c  cmd_status.c  cmd_update.c
│   ├── cmd_prune.c  cmd_branch.c  cmd_relocate.c  cmd_lang.c
│   ├── cmd_publish.c  cmd_hook.c  cmd_help.c
│   ├── shared.c/.h  state.c/.h  chain.c/.h  merge.c/.h
│   ├── peers.c/.h  hooks.c/.h
│   ├── git.c/.h
│   ├── bounds.h
│   └── msg.c/.h
├── utils/                    moduli di base, senza dipendenze da src/
│   ├── proc.c/.h  proc_win.c  proc_posix.c
│   ├── sys.h  sys_win.c/.h  sys_posix.c
│   ├── str.c/.h  mem.c/.h  path.c/.h
│   └── err.h
├── help/                     pagine di aiuto, una per comando (D-087)
│   └── it/                   le stesse in italiano (D-088)
├── tools/                    embed.c: pagine incorporate, HTML e man (build)
├── doc/                      diagrammi draw.io
├── test/
│   ├── lib.sh                funzioni comuni: repo di prova, asserzioni
│   └── t0001-<scenario>.sh   uno scenario per file, numerati come i test di git
└── build/                    generato
```

### 6.3 Formati dati e file del protocollo
Tutti i file in formato config di git si leggono e scrivono **solo** con `git config` (`-f <file>` per quelli fuori da `.git/config`): nessun parser nostro.

| Dato | Dove | Versionato | v0 |
|---|---|---|---|
| Dichiarazione dei file shared | `.gitshared` alla radice | sì | sì |
| Configurazione locale della repo | `.git/config`, sezione `[shared]` | no | sì |
| Dati locali di un file shared | `.git/config`, sezione `[shared "<id>"]` | no | sì |
| Lingua del manuale | `~/.gitconfig`, `shared.lang` | no | sì (D-088) |
| Catena delle versioni | `refs/shared/<id>` | no (replicata tra peer) | sì |
| Versioni ottenute da `update` | `refs/shared-incoming/<id>/<k>` | no, temporanee | sì |
| Hook | blocco con marker in `hooks/<evento>` | no | sì |
| Unione in corso | `refs/shared-merge/<id>` | no | sì |
| Cache della base | `.git/shared/base-cache` | no, eliminabile | no |

#### `.gitshared`
```ini
# Managed by 'git shared': do not edit by hand.
# This repository shares files with other repositories: run 'git shared status'.
[shared "common.h"]
	id = 0f8c3e5a9b2d4c71a6e0d9b8c7f61234
	path = include/common.h
	branch = main
```
- Una sezione per file shared; il nome della sezione è il nome breve.
- `id`: 32 caratteri esadecimali minuscoli (128 bit, D-006).
- `path`: relativo alla radice della repo, separatore `/`, mai assoluto (D-003).
- `branch`: zero o più, default dei branch attivi (D-057).
- `url`: fase 2.
- Il commento in testa è il segnale per chi clona la repo (D-048); `git config` conserva i commenti (verificato, anche dopo `--remove-section`).
- In lettura (`git config -f .gitshared --get-regexp '^shared\.'`) le chiavi arrivano come `shared.common.h.id`: il nome breve può contenere punti, quindi si separa all'**ultimo** punto.
- v0: `set` rifiuta un nome già presente; la regola definitiva è A-24.

#### `.git/config`
```ini
[shared]
	selfPath = C:/Users/luca/progetti/A
[shared "0f8c3e5a9b2d4c71a6e0d9b8c7f61234"]
	branch = main
	peer = C:/Users/luca/progetti/B
	peer = C:/Users/luca/progetti/C
	pending = link
	pendingSource = C:/Users/luca/progetti/B
	pendingBranch = main
	pendingPath = include/common.h
	pendingCommit = 9fceb02d0ae598e95dc970b74767f19372d61af8   ; solo unlink
```
- `shared.selfPath`: radice della repo stessa, come la scrive `rev-parse --show-toplevel` (D-035, D-084). Scritto al primo comando con almeno un file collegato e da `relocate`.
- `shared.<id>.branch`: branch attivi, nomi esatti nella v0 (D-055).
- `shared.<id>.peer`: radice di ogni peer, come la scrive `rev-parse --show-toplevel` nella sua repo; al massimo 32 (D-034; nella v0 il limite si controlla solo al completamento di `link`, reportIdea B-38).
- `shared.<id>.pending` (`set`, `link`, `unlink`) e `pendingSource` (solo `link`): operazione in attesa di commit (REQ-F-023). `pendingBranch`: branch su cui è stato lanciato il comando (assente con HEAD staccata, D-077). `pendingCommit` (solo `unlink`): HEAD al comando; la conferma è un suo discendente senza la sezione (D-081). `pendingPath`: path del file, solo per i messaggi, perché un'attesa vista da un altro branch non ha la sezione in `.gitshared`. Si scrive `pending` per ultima e si toglie per prima: senza `pending` le altre chiavi sono ignorate. Spariscono quando l'operazione si completa o viene scartata.
- **Niente `name` né `path`** (D-068): stanno in `.gitshared`, che è per branch; duplicarli in config crea incoerenze (A-21).

#### Catena `refs/shared/<id>`
- Ogni versione è un commit il cui tree ha **una sola voce**: `100644 blob <oid>	content` (D-042). Modo sempre `100644` nella v0 (bit di esecuzione: A-15).
- Genitori: nessuno per la prima versione; uno per una versione normale e per l'unione di una modifica del branch con T; due per l'unione di due versioni (D-078). Il primo genitore è sempre la T locale di partenza. La catena si legge come grafo, con entrambi i genitori (D-091): ogni versione raggiungibile da T è una versione, anche quella di un peer entrata come secondo genitore.
- Autore e data: quelli dell'utente, come per ogni commit (D-013). Messaggio: `shared: <nome>`. Commit deterministici: A-14.
- Creazione con il solo plumbing: `git mktree` (riga `100644 blob <oid>\tcontent`), `git commit-tree <tree> [-p <T>] -m <msg>`, `git update-ref refs/shared/<id> <nuovo> <T>`. L'ultimo argomento rende l'aggiornamento condizionato (REQ-NF-005); per la prima versione si passa il valore vuoto, che richiede che la ref non esista.
- Lettura: blob di T = `git rev-parse refs/shared/<id>:content`; ordine = `git merge-base --is-ancestor`.
- Pubblicazione: `git push <peer> refs/shared/<id>:refs/shared/<id>` (D-040), senza `+`: il fast-forward lo verifica git.

#### Ref temporanee di `update` e `link`
`git fetch <peer> +refs/shared/<id>:refs/shared-incoming/<id>/<k>`, con `k` = posizione del peer nell'elenco. Qui il `+` è corretto: sono copie di lavoro. `update` le cancella alla fine (`update-ref --stdin`, un processo per tutte). `link` tiene `refs/shared-incoming/<id>/0` fino al commit: protegge dal gc gli oggetti ricevuti e al completamento diventa `refs/shared/<id>`; se l'attesa viene scartata, si cancella. Il prefisso `refs/shared-incoming/` non ricade sotto `refs/shared/`, quindi non si confonde con le catene.

Fetch in parallelo nella stessa repo: `--no-write-fetch-head` (nessun FETCH_HEAD condiviso) e `--no-auto-gc` (git ≥ 2.29, entro il minimo 2.34).

#### Unione in corso `refs/shared-merge/<id>` (D-078)
- Equivalente di `MERGE_HEAD`, come ref: atomica (`update-ref`), visibile con i comandi git, e protegge dal gc gli oggetti della versione del peer.
- Punta a un commit provvisorio, mai inviato ai peer, i cui **genitori** sono le versioni che la risoluzione incorpora: la T di partenza e, per una divergenza tra versioni, la versione del peer. Il suo tree non conta.
- Nasce solo con un conflitto. Si toglie alla conclusione o con `update --abort` (D-093). Vale finché il path ha stage di conflitto nell'index oppure il blob nell'index è diverso da H. Altrimenti è scaduta (`git restore`, o risoluzione committata prima di `update`): la cancella il primo comando che ricava lo stato (`state_derive`, D-092); `update` lo dice con un avviso.
- Unione a 3 vie con il solo plumbing: `update-index -z --index-info` (voce di stage 0 tolta, stage 1/2/3 = base, nostra, loro), `merge-index git-merge-one-file -- <path>`; in caso di conflitto `checkout -m -- <path>` riscrive i marker con le etichette `ours`/`theirs` e lo stile di `merge.conflictStyle`. Base assente: blob vuoto (`hash-object -w --stdin`).

#### Hook (v0: blocco con marker)
- Installati da `set` e `link` al comando (REQ-F-016, D-074): `post-checkout`, `post-merge`, `post-rewrite`. `git shared hook install` aggiunge i blocchi che mancano e toglie quelli delle versioni precedenti (`pre-commit` della prima versione di M3, `post-commit` di `autoPublish`). Un file di hook che dopo la rimozione contiene solo `#!/bin/sh` era stato creato da git shared e si cancella.
- Cartella degli hook da `git -C <radice> rev-parse --git-path hooks`, che rispetta `core.hooksPath`.
- Blocco, uguale per ogni evento salvo il nome:
  ```sh
  # >>> git shared >>>
  git-shared hook post-merge "$@" </dev/null || :
  # <<< git shared <<<
  ```
  - `git-shared` diretto e non `git shared`: un processo in meno a ogni hook (D-069). Git mette nel `PATH` degli hook le cartelle in cui trova i comandi esterni.
  - `</dev/null`: lo stdin resta all'hook che ci ospita (es. l'elenco che riceve `post-rewrite`).
  - `|| :`: se `git-shared` manca o fallisce, l'esito dell'hook non cambia. Il blocco non contiene `exit`.
- Hook assente: si crea il file con `#!/bin/sh` e il blocco, con bit di esecuzione.
- Hook presente: il blocco si inserisce **subito dopo la prima riga**, non in fondo: un hook che termina con `exit` non lo raggiungerebbe. Mai sovrascrivere (D-030). Un blocco già presente non si duplica.
- Il modo di un hook presente non cambia, né quando si inserisce il blocco né quando lo si toglie: un hook che l'utente ha reso non eseguibile resta disattivato, blocco compreso (D-089).
- Solo negli hook di shell (D-096): senza `#!`, o con interprete `sh`, `bash`, `dash`, `zsh`, `ksh`, `mksh`, `ash` (anche dopo `env` o `busybox`). Un binario (byte nullo) o un altro interprete non si tocca, con un avviso; se ha già il blocco di una versione precedente, il blocco si toglie.

### 6.4 Flussi principali
Regola per tutti i flussi: **pochi processi git**, perché su Windows ognuno costa ~30 ms (§6.1). Processi indipendenti si avviano insieme; molte domande sugli oggetti passano da un solo `cat-file --batch-check`.

#### `status`
1. In parallelo: `rev-parse --show-toplevel --show-prefix`, `symbolic-ref -q HEAD` (branch corrente: funziona anche su un branch senza commit, `rev-parse HEAD` no), `config -z --show-scope --get-regexp ^shared\.` (sezioni locali).
2. `config -z -f <top>/.gitshared --get-regexp ^shared\.`, mentre gli altri due finiscono.
3. Un `cat-file --batch-check` per tutte le domande: `HEAD:<path>` (H) e `refs/shared/<id>:content` (T) di ogni file.
4. Solo se c'è un'operazione in attesa: `config --blob HEAD:.gitshared` e il completamento.
5. Solo se H ≠ T e I ≠ T: blob di tutte le versioni, visitando il grafo della catena sullo stesso `cat-file` (tre domande per versione, D-091), e un `git log --raw --root --no-follow --diff-merges=off --no-show-signature` sul path del file (D-065, D-094), interrotto appena possibile. Scorciatoia: se il blob-versione più recente nella storia è quello di T, la base è T e la lettura si ferma. Quando lo stesso contenuto compare in più versioni può dare una base diversa dalla regola di D-065 (catena v1(A) v2(B) v3(A), storia A poi X: per la regola *divergente* con base v1, per la scorciatoia *da pubblicare* da v3), ma l'unione di (A, X, A) dà X: il contenuto che arriva ai peer è lo stesso.

6. Solo per una sezione locale che il `.gitshared` del working tree non dichiara: `HEAD:.gitshared` sul `cat-file`; se non la dichiara, `for-each-ref refs/heads` e i `.gitshared` distinti dei branch (D-095).

Caso comune (tutti i file allineati): **5 processi, qualunque sia il numero di file**.

Con `--peers` (D-086), per ogni file e in parallelo verso i peer: `git --git-dir=<peer>/.git config --local --get-regexp` (come `prune`) e `rev-parse -q --verify refs/shared/<id>^{commit}`. Se la punta del peer è diversa da T e l'oggetto c'è anche qui, `merge-base --is-ancestor <peer> <T>`: vero = il peer è indietro. Altrimenti (oggetto assente, avanti o divergente) il peer ha una versione che qui non c'è.

#### `set`
1. Come `status`, passi 1–4; poi `ls-files -s --full-name` con pathspec letterali: risolve il path, controlla che sia tracciato, senza conflitti e un file regolare.
2. ID da 16 byte casuali (UUID v4).
3. Attesa in `.git/config` **per prima**: se un passo successivo fallisce, il primo comando la scarta da solo (REQ-F-023).
4. Sezione in `.gitshared` (`git config -f`: id, path, branch); in caso di errore `.gitshared` torna com'era.
5. `update-index --add -- .gitshared`.
6. Con `-c`/`-m`: `--literal-pathspecs commit --only -- .gitshared` (D-075), poi il completamento.

#### `link`
1. Sorgente: `rev-parse --show-toplevel --show-prefix --show-object-format` nella cartella del file indicato (un processo); stesso algoritmo di hash (REQ-NF-009); voce nel suo `.gitshared` (D-019).
2. Destinazione: path della sorgente o quello dato (D-028); non deve essere già shared, avere un nome già usato o esistere (v0).
3. `fetch` della catena in `refs/shared-incoming/<id>/0`.
4. Attesa con `pendingSource` **per prima**; sezione in `.gitshared`; file dal blob di T (`update-index --cacheinfo` + `checkout-index`, D-042); staging.
5. Con `-c`/`-m`: commit dei soli `.gitshared` e file (`--only`, D-075), poi il completamento.

#### `relocate` (D-084)
Vecchio path: `shared.selfPath`, oppure l'argomento reso assoluto anche se non esiste più (`sys_abs_path`: si risolve la cartella che lo contiene, con i nomi lunghi su Windows, e si aggiunge l'ultimo nome). Spostamento: in ogni peer, in parallelo, `config --replace-all shared.<id>.peer <nuovo> '^(<vecchio>|<nuovo>)$'` (espressione con i caratteri speciali protetti): una sola voce anche se si ripete, aggiunta se mancava. Copia: `config --add` del vecchio path in locale se condivide ancora il file (`peers_check`), poi l'annuncio come al `link`. `--peer`: `repo_locate` del nuovo path, `peers_check` per la sezione, `--replace-all` in locale. Infine `config shared.selfPath <radice>`.

#### `branch` (D-083)
Elenco: `for-each-ref --format=%(refname:short) refs/heads` (branch esistenti) e `config -f .gitshared --get-regexp` per il default. `--add`: `check-ref-format refs/heads/<nome>` per ogni nome, poi `config --add`; `--remove`: `config --fixed-value --unset-all`. L'elenco finale si calcola prima in memoria: un `--remove` di un nome assente o un elenco locale vuoto si rifiutano prima di scrivere. Perché non vuoto: un file senza peer né branch avrebbe la sezione `[shared "<id>"]` senza chiavi, che git non mostra più, e il file risulterebbe *scollegato* (provato). `--default` lavora su `.gitshared` con `-f` e lo mette in staging.

#### `prune` (D-082)
Per ogni file: verso ogni peer in parallelo `git --git-dir=<peer>/.git config --local --get-regexp ^shared\.<id>\.` (codice 0 membro, 1 uscito, altro: sparito se `<peer>/.git` non esiste su disco, altrimenti irraggiungibile). `--local` è necessario: senza, verso una cartella che non esiste git legge solo la configurazione globale e risponde 1, e un peer cancellato sembrerebbe uscito (provato con git 2.43). Poi `config --fixed-value --unset-all shared.<id>.peer <peer>` in locale e, in parallelo, in ogni peer membro (codice 5 = non c'era). Ref senza sezione: `for-each-ref` su `refs/shared`, `refs/shared-merge`, `refs/shared-incoming` e un solo `update-ref --stdin`.

#### `publish`
Per ogni file: *da pubblicare* → `mktree` + `commit-tree -p T` + `update-ref` condizionato a T; poi, anche se *allineato*, `push --porcelain --no-verify` (senza `+`; il `pre-push` dell'utente non gira, D-080) verso tutti i peer **in parallelo**. Esito per peer: codice 0 inviato o già aggiornato; codice 1 con una riga `!` rifiutato (il peer ha una versione nuova: "non-fast-forward" o "fetch first"); altro irraggiungibile (REQ-F-008).

#### `update`
Per ogni file:
1. `fetch` dai peer **in parallelo**; `merge-base --independent T I1 … In` (un processo). Se T non è tra le versioni rimaste, una la contiene: è l'unica rimasta, oppure la prima per cui `merge-base --is-ancestor T <versione>` è vero (un processo per versione, tranne l'ultima, che se nessun'altra la contiene è lei; D-092). `update-ref` condizionato (fast-forward). Le altre versioni rimaste sono da unire.
2. Se `refs/shared-merge/<id>` esiste: scaduta → si cancella; path ancora in conflitto → errore; risoluzione in staging → unione di (blob della T di partenza, blob nell'index, blob di T), che con T ferma è la risoluzione stessa, e nuova versione con i genitori registrati.
3. Lo stato: *da applicare* → controllo delle modifiche locali (REQ-F-010, D-076 e D-094: `-c diff.autoRefreshIndex=true diff --quiet` tra index e working tree, poi blob dell'index confrontato con H e con le versioni della catena sullo stesso `cat-file`) e T nel file (D-042); *da committare* → niente da scrivere; *divergente* → unione di (blob della base, H, blob di T).
4. Per ogni versione rimasta al passo 1: `merge-base T P`; se è P stessa, T la contiene già (l'ha appena unita il passo 2) e si salta (D-092); altrimenti unione di (blob del merge-base, contenuto corrente, blob di P), versione con genitori T e P.
5. Con `-c`/`-m` commit dei soli file applicati, uniti o da committare (D-075).

`--abort` (D-093): nessun fetch. Per ogni file scelto con `refs/shared-merge/<id>`: blob di `HEAD:<path>` sul `cat-file`, scritto nell'index e nel file come per un'applicazione (`update-index --cacheinfo`, che sostituisce anche gli stage di conflitto, e `checkout-index`), poi la ref si cancella. Prima il file, poi la ref: se il file non si scrive, il comando si può ripetere.

Un'unione pulita costa 4 processi (index, `merge-index`, lettura del risultato, nuovo `cat-file` che veda l'index) più i 3 della versione; i casi senza unione vera (una delle due parti uguale alla base) non passano dagli stage.

#### Completamento di un `link` (REQ-F-002, D-059)
Come `set` per i branch attivi; poi la ref temporanea diventa `refs/shared/<id>` (con un nuovo fetch se manca); membership = peer della sorgente + la sorgente, senza se stessa (massimo 32, D-034); annuncio in parallelo a ogni peer con `config --replace-all --fixed-value shared.<id>.peer <radice>` (nessun duplicato, un processo per peer); per ultima la rimozione dell'attesa. Un peer irraggiungibile produce un avviso e non blocca (B-07).

#### Hook (`git shared hook <evento>`, M3)
Escono sempre con 0: un problema diventa un avviso. Git li lancia dalla radice del working tree.
- **Percorso rapido (D-069).** Nessun `./.gitshared` → uscita senza processi.
- **Nessun hook sui commit** (D-074). Un blocco `pre-commit` o `post-commit` rimasto da una versione precedente chiama ancora `git-shared hook <evento>`, che non fa nulla.
- **`post-checkout`** (solo checkout di branch), **`post-merge`**, **`post-rewrite`.** Per ogni file sul branch attivo non allineato: avviso con lo stato e il comando da lanciare (REQ-F-013). Nessun avviso per i file sospesi. Non girano sui commit, quindi non li rallentano (`post-rewrite` gira su `--amend` e `rebase`).
- I comandi che committano da sé (`set -c`, `link -c`, `update -c`) dopo il commit aprono un nuovo `cat-file` e completano le attese. Il completamento si può ripetere senza effetti (chiavi già tolte, peer già presenti).

#### Completamento di un `unlink` (REQ-F-003, D-081)
Conferma cercata prima sul branch del comando e su HEAD; su tutti i branch locali solo se si sta per annullare (sezione tornata nel working tree sul branch del comando) o se il branch del comando non c'è più. Per ogni punta distinta: blob di `<rev>:.gitshared` sul `cat-file` già aperto (ogni blob distinto letto una volta con `config --blob`); se non dichiara l'ID, `merge-base --is-ancestor <pendingCommit> <rev>`. Trovata: (1) `config --fixed-value --unset-all shared.<id>.peer <radice>` in ogni peer, in parallelo (codice 5 = già tolto); (2) `update-ref --stdin` cancella `refs/shared/<id>`, `refs/shared-merge/<id>` e le `refs/shared-incoming/<id>/*`; (3) `config --remove-section shared.<id>`, che toglie anche l'attesa. L'ordine permette di ripetere dopo un'interruzione.

#### Completamento di un `set` (REQ-F-023)
Quando la sezione con quell'ID compare in `refs/heads/<pendingBranch>:.gitshared` o, in mancanza, in `HEAD:.gitshared` (D-077; un `config --blob` per revisione, solo se c'è un'attesa): (1) branch attivi copiati dal default di `.gitshared`, o il branch del comando; (2) prima versione della catena (`mktree` dallo stdin, `commit-tree`, `update-ref` con valore vecchio vuoto: solo se la ref non esiste); (3) rimozione dell'attesa. L'ordine permette di ripetere tutto dopo un'interruzione. Il blob del file si prende dalla stessa revisione; se il file non c'è ancora, l'attesa resta e l'utente riceve un avviso. Se la sezione non è in nessuna delle due revisioni né nel working tree, e si è sul branch del comando, si cerca l'ID nel `.gitshared` degli altri branch locali (`for-each-ref refs/heads`, poi il blob di `<branch>:.gitshared` sul `cat-file` già aperto; ogni blob distinto si legge una volta sola): se un branch lo dichiara si completa da lì, altrimenti l'attesa viene scartata (D-079). Il `link` si completa allo stesso modo.
### 6.5 Gestione errori
Ogni funzione che può fallire restituisce un `eGsErr_t` (`utils/err.h`: uso, fuori da una repo, processo non avviato, git con errore, formato inatteso, sistema; §8.4). Ogni comando traduce l'esito in un messaggio della tabella (§8.5) e in un codice di uscita come git (REQ-NF-008): 0 successo; 1 errore (modifiche locali, push rifiutato, conflitto aperto, file in attesa); 128 errore che impedisce di proseguire (dialogo con git interrotto, stdout chiuso); 129 uso sbagliato.
- Gli hook escono sempre con 0: un problema diventa un avviso (§6.4).
- Un peer irraggiungibile è un avviso, non un errore: gli altri peer ricevono comunque (REQ-F-008).
- Le scritture si ordinano perché un errore a metà si possa ripetere: l'attesa si scrive per prima e si toglie per ultima (REQ-F-023), la catena avanza solo con `update-ref` condizionato (REQ-NF-005).

## 7. Strategia di verifica
<!-- Matrice di tracciabilità V-cycle: REQ ↔ test di sistema / integrazione / unità. -->
> Embrionale: per ora solo test di sistema, che lanciano `git shared` come farebbe l'utente.

- **Struttura.** `test/t<NNNN>-<scenario>.sh`, uno scenario per file; `test/lib.sh` con le funzioni comuni; `test/run.sh` li esegue tutti (`make test`).
- **Isolamento.** Ogni file lavora in una cartella temporanea cancellata alla fine, con `build/` in testa al `PATH`, `HOME` temporanea e `GIT_CONFIG_NOSYSTEM=1`: la configurazione git dell'utente non influisce.
- **Formato.** Output TAP (`ok N - ...`, `not ok N - ...`, piano `1..N`); codice di uscita 1 se un test fallisce. Un test che su una piattaforma non ha senso (es. il bit di esecuzione su Windows) esce come `ok N - ... # SKIP <motivo>`. `run.sh` scrive in testa la versione di git e il sistema.
- **Matrice (D-032).** Si prova la git che il `PATH` trova. **Dal 2026-11-17 (D-098) la suite si lancia solo con l'ultima stabile**, 2.56.1, sulle due piattaforme: la tabella sotto resta come storia delle prove fatte fino a D-096.

  | Sistema | git | Come |
  |---|---|---|
  | Windows 11 | 2.43 (Git for Windows installato) | `make test` da Git Bash |
  | Windows 11 | **2.56.1**, ultima stabile al 2026-11-17 (PortableGit) | `make test` dalla bash della PortableGit: `%USERPROFILE%\git-latest\bin\bash.exe -l` |
  | Linux, WSL2 Ubuntu 22.04 | **2.34**, il minimo (pacchetto della distribuzione) | `make test` |
  | Linux, WSL2 Ubuntu 22.04 | **2.56.1**, ultima stabile al 2026-11-17 | `PATH="$HOME/git-latest/bin:$PATH" make test` |

  L'ultima stabile su Linux si compila dai sorgenti in `~/git-latest`, senza toccare la git del sistema: `make prefix=$HOME/git-latest NO_RUST=1 NO_CURL=1 NO_PERL=1 NO_PYTHON=1 NO_TCLTK=1 NO_GETTEXT=1 NO_OPENSSL=1 install` (servono `build-essential`, `zlib1g-dev`, `libexpat1-dev`). Dalla 2.55 git compila per default una parte in Rust; `NO_RUST` la esclude finché resta facoltativa (fino a Git 3.0). A ogni nuova versione stabile si ricompila e si aggiorna questa tabella.

  Su Windows l'ultima stabile è la **PortableGit** di Git for Windows (`PortableGit-<versione>-64-bit.7z.exe` dalla release su GitHub, hash e firma controllati), estratta in `%USERPROFILE%\git-latest` senza toccare la git installata. I test si lanciano dalla sua bash e non dalla Git Bash installata: due runtime MSYS diversi nello stesso albero di processi non convivono. Il compilatore resta MinGW-w64 dal `PATH` di Windows.

  **Compatibilità chiusa (D-090, 2026-11-17).** Tre righe su quattro della matrice sono provate. git 2.34 su Windows non è provata ed è ritenuta stabile: i comandi git usati sono gli stessi sulle due piattaforme e con 2.34 passano su Linux, e il codice proprio di Windows (`proc_win.c`, `sys_win.c`) non dipende dalla versione di git. Per provarla basta una PortableGit 2.34.1 in un'altra cartella, lanciata come la riga 2.56.1.

| Test | Requisiti | Test |
|---|---|---|
| `t0001-usage.sh` | REQ-F-018, REQ-F-019, REQ-NF-008, D-067, D-087: uso, `-h`, pagina generale, pagina di ogni comando con `<comando> --help` e `help <comando>`, argomento sconosciuto, `git-shared --help` | 65 |
| `t0002-status.sh` | REQ-F-012 (parte), REQ-F-021, REQ-NF-003 e D-067 (path con spazi e non ASCII, senza shell), §6.3; git in una cartella `git-José` (junction su Windows, link su Linux) indicata da `GIT_EXEC_PATH` (C-05, D-097) | 29 |
| `t0003-set.sh` | REQ-F-001, REQ-F-021, REQ-F-022 (sospensione), REQ-F-023 (attesa, completamento, annullamento), D-006, D-007, D-057; path con spazi e non ASCII (reportIdea C-01) | 68 |
| `t0004-base.sh` | REQ-F-012, D-054, D-065 (revert, `reset`, B-14), con catene di più versioni costruite a mano; stato uguale con `log.showRoot=false` (file nato nel commit radice, anche `publish`), `log.follow=true` (file rinominato con un contenuto vecchio uguale a una versione) e `diff.autoRefreshIndex=false` (file solo toccato, `update` applica) (B-32, D-094) | 17 |
| `t0005-peers.sh` | REQ-F-002, REQ-F-004…REQ-F-010, D-028, D-040, D-059: link, membership e annuncio, publish, update, modifiche locali, divergenze, peer irraggiungibile, push rifiutato; link, publish e update con path non ASCII (reportIdea C-01); `link -c` che committa solo file e `.gitshared` (D-075). File condiviso: `breef.md` del progetto | 93 |
| `t0007-tocommit.sh` | REQ-F-009, REQ-F-010, REQ-F-012, D-075, D-076: stato *da committare* dopo `update` senza commit; seconda versione applicata senza `-f`; modifica dell'utente nel working tree o in staging protetta (reportIdea B-26, B-30); `update -c` da *da committare*; annullamento con `git restore` | 31 |
| `t0008-pending-branch.sh` | REQ-F-023, D-059, D-077: chiavi dell'attesa; `set` e `link` committati e completati da un altro branch (reportIdea B-29); `set` non committato portato su un altro branch e committato lì; switch rifiutato da git per conflitto su `.gitshared`; `git restore` scartato solo sul branch del comando; branch del comando cancellato; commit di conferma su un altro branch e primo comando sul branch del comando (B-39), anche con molti branch; attesa scartata durante `git stash` e ripresa da `set` con lo stesso ID, prima e dopo il commit della sezione (B-40, D-079); `link` scartato da uno stash e ripreso da `link`, destinazione diversa rifiutata; clone ricollegato da `link` senza toccare il file né metterne in staging le modifiche, con membership, hook e ciclo publish/update (D-043, D-080) | 79 |
| `t0009-merge.sh` | REQ-F-010, REQ-F-011, REQ-F-012, D-029, D-078: unione pulita del branch (un genitore) e `update -c`; conflitto con stage e marker, stato *unione in corso*, `publish` rifiutato, risoluzione e conclusione; abbandono con `git restore` (reportIdea B-13); T che avanza durante la risoluzione (B-17); due peer che pubblicano senza vedersi (due genitori); T che avanza solo sulla versione ricevuta che la contiene, con la versione che la contiene seconda nell'ordine dei peer (B-47); unione abbandonata tolta da `status` e da `publish`, modifica in staging dopo l'abbandono protetta e nessuna versione nuova (B-48); `update --abort` con il conflitto aperto e con la risoluzione in staging, per tutti i file e per uno, senza unione e con `-c`/`-f` (D-093); conclusione di un conflitto tra versioni con una sola versione di unione (B-49, D-092); peer la cui versione è stata unita da un'altra repo: *da applicare*, `update` applica senza rimettere ciò che l'altra repo ha tolto, la sua versione in staging non è una modifica locale (B-46, D-091); squash e amend dopo `publish` (B-42, B-34); `link` con modifica prima del commit, senza base (B-41); modifiche locali protette | 141 |
| `t0010-unlink.sh` | REQ-F-003, REQ-F-023, D-081: argomenti, uso e manuale; attesa e `.gitshared` tolto se vuoto; annullamento con `git restore`; commit e pulizia; `-c`/`-m` con due file; peer che dimenticano la repo, anche con un peer irraggiungibile; commit su un altro branch; branch nato prima del comando che non conferma; `set` in attesa rifiutato; file scollegato (clone); file cancellato; sezione orfana per ID (reportIdea B-45) e rifiuto se un branch la dichiara (B-16) | 65 |
| `t0011-prune.sh` | REQ-F-014, D-082: argomenti, uso e manuale; nessun peer da togliere; peer cancellato (anche `-n`), tolto anche dagli altri peer, `publish` senza avvisi dopo; cartella rimasta senza `.git`; peer uscito mentre questa repo era irraggiungibile (reportIdea B-07); peer illeggibile che resta; `prune <path>`; ref senza sezione (B-27, C-22) tolte e ref con sezione intatte; `link` in attesa non toccato | 42 |
| `t0012-branch.sh` | REQ-F-022, D-057, D-083: argomenti, uso e manuale; forme brevi `-a` e `-rm`; elenco con branch corrente e nomi inesistenti; `--add`/`--remove` con effetto immediato, senza doppioni, in qualunque ordine; branch che non esiste ancora; pattern e nomi non validi rifiutati; elenco vuoto rifiutato senza scrivere; `--reset`; `--default` in staging, con `-c` e `-m` (solo `.gitshared` nel commit); `link` che usa il default della repo che si collega; clone ricollegato che copia il default; file scollegato e `set` in attesa; branch rinominato (reportIdea B-21) | 75 |
| `t0013-relocate.sh` | REQ-F-015, REQ-F-017, D-084: `selfPath` scritto e nessun avviso al suo posto; argomenti, uso e manuale; repo non spostata e repo senza file collegati; spostamento con avviso, `relocate`, peer aggiornati, `publish`/`update` dopo; ripetuto con il vecchio path relativo senza doppioni; copia rifiutata senza `--copy` e accettata con `--copy` (reportIdea B-22); `--copy` di una repo sparita; `--peer`, ripetuto, con il vecchio path ancora presente, verso una repo che non condivide; worktree secondario senza avviso | 49 |
| `t0014-avvisi.sh` | REQ-F-012, REQ-F-013, REQ-F-022, D-085: nessuna nota al suo posto o con un branch attivo esistente; branch rinominato: nota in `status`, avviso dell'hook al checkout, in `update` e `publish`; nessuna nota dopo la correzione e con un branch futuro accanto a uno esistente (reportIdea B-21); `publish`/`update <file>` su un file in attesa o scollegato (errore, codice 1) e senza argomento (riga per il file saltato) (B-37); branch nato prima del `set`: nessun avviso dell'hook, `status` *sospeso* con path e branch che lo dichiarano, `update` e `publish` innocui; sezione tolta a mano dal `.gitshared` del branch che la dichiara: *incoerente* (B-16, D-095) | 38 |
| `t0015-status-peers.sh` | REQ-F-012, D-086: argomenti, uso e manuale; senza `--peers` nessuna riga; tutti allineati, nessuna scrittura nei peer; peer spostato (non trovato), indietro (`publish`), avanti (`update`), divergente con l'oggetto presente; peer uscito, senza versione, illeggibile; file senza peer e file in attesa | 22 |
| `t0016-lang.sh` | REQ-NF-008, REQ-F-019, D-088: `lang` senza argomenti, lingua sconosciuta, troppi argomenti, uso; `-h` generale con `help`, `--help`, `<comando> --help` con esempio e `lang it`; `lang it` nella config globale; pagina generale e pagina di ogni comando in italiano, anche dentro una repo; `-h` dei comandi resta in inglese; ritorno all'inglese; HTML e man in italiano da `embed` | 59 |
| `t0006-hooks.sh` | REQ-F-004, REQ-F-013, REQ-F-016, REQ-F-023, D-030, D-069, D-073, D-074: solo hook di avviso, commit senza output e senza pubblicazione anche con `shared.autoPublish` rimasto in config; `hook install` toglie i blocchi `pre-commit` e `post-commit` delle versioni precedenti senza toccare l'hook dell'utente; un blocco rimasto non fa nulla; avvisi al checkout; manuale; `publish` non esegue il `pre-push` dell'utente (reportIdea C-20, D-080); modo degli hook: il blocco entra e esce senza cambiare i permessi dell'hook dell'utente, un hook disattivato resta disattivato, solo l'hook creato da git shared è eseguibile (reportIdea C-25, D-089; 5 test saltati su Windows); hook Python, Perl e binario intatti con un avviso, anche con `hook install`; blocco in `env bash`, `sh -e`, `busybox sh`; blocco di una versione precedente tolto da un hook Python (C-02, D-096) | 62 |

## 8. Stile di programmazione
> Preferenze dell'autore (background embedded, orientamento MISRA). Sezione viva: si aggiunge man mano. Vale per le parti scritte in C (D-063).

### 8.1 Standard
- **C11** (D-031), solo il sottoinsieme: `_Static_assert`, struct/union anonime, `_Alignas`/`_Alignof`, `_Noreturn`. Vietati VLA, `<threads.h>`, Annex K. Nota: "ANSI C" in senso stretto è C89/C90.
- Per confronto: git richiede C99 dalla v2.35 e compila con `-std=gnu99` (con fallback C11 per MSVC nella build meson).
- Compilatori di riferimento: GCC e Clang (Linux), MSVC e/o MinGW-GCC (Windows).

### 8.2 Tipi e strutture
- Tipi a larghezza fissa di `<stdint.h>` (`uint8_t`, `uint32_t`, ...) ovunque la dimensione conti; `size_t` per dimensioni e indici di buffer.
- `typedef` per tutte le struct, con nome in **PascalCase** seguito da `_t` (es. `typedef struct { ... } SharedEntry_t;`).
- `typedef` degli enum: prefisso `e`, poi PascalCase e `_t` (es. `eGsErr_t`, `eMsgId_t`). I puntatori a funzione restano in snake_case con `_t` (es. `cmd_fn_t`).
- Letterali senza segno con suffisso **`U` maiuscolo** (`0U`, `4096U`, `0x1U`). MISRA C:2012 (Rule 7.2) chiede il suffisso; la maiuscola è scelta di stile, coerente con il divieto della `l` minuscola (Rule 7.3).
- **Bitfield** consentiti per flag e stati **in memoria**. Vietati per mappare formati binari esterni (file, protocolli): layout, ordine dei bit e padding dei bitfield sono definiti dall'implementazione. Tipi ammessi per i bitfield: `unsigned int` / `uint32_t` (o `_Bool`), mai `int` con segno.

### 8.3 Memoria
- **Allocazione dinamica ammessa** (D-066), con disciplina: ogni allocazione passa da un wrapper (`xmalloc`, `xrealloc`, sul modello di git) che, se l'allocazione fallisce, stampa un messaggio ed esce. Il resto del codice non controlla mai `NULL`.
- Nessun obbligo di liberare la memoria prima dell'uscita: il processo dura millisecondi e il sistema operativo recupera tutto. Si libera solo dove un ciclo farebbe crescere la memoria senza limite.
- Dove non serve avere tutto insieme, si preferisce comunque l'**elaborazione di un elemento alla volta** (una sezione di `.gitshared`, un peer, una riga di output).
- I limiti che restano (D-034) sono costanti dichiarate, verificate con test ai bordi. Limiti, allocazione e stringhe sono isolati in un solo modulo: un header con le costanti, il wrapper di allocazione e un tipo stringa con le sue funzioni.
- Motivazione: il divieto MISRA nasce per sistemi a lunga vita (frammentazione, tempi non deterministici); per un comando locale che dura millisecondi non serve, e i limiti statici obbligavano a inventare tetti artificiali, come il numero di versioni della catena (D-065). È una deviazione consapevole dall'orientamento MISRA di §8. Per confronto, git usa l'heap ovunque (`xmalloc`, `strbuf`, `mem_pool`).

### 8.4 Controllo di flusso e gestione errori
- Pattern **`goto exit`** per la pulizia in un unico punto di uscita: solo salti in avanti, verso un'etichetta nello stesso blocco funzione (compatibile con MISRA C:2012 Rule 15.2/15.3).
- Ogni funzione che può fallire restituisce un codice di errore (enum); nessun errore ignorato.

### 8.5 Testi e messaggi
- Messaggi utente in tabelle statiche di stringhe (`const char *const msg_en[MSG_COUNT]`, `msg_it[...]`) indicizzate da un `enum`: nessuna allocazione, nessuna dipendenza (niente gettext). Nella v0 c'è solo `msg_en`: i messaggi dei comandi sono in inglese, il manuale anche in italiano (D-088).

## 9. Registro decisioni
Le decisioni sono in ordine di ID, cioè cronologico. Il ragionamento che le lega, per argomento, è in §9.1. La colonna *Origine* cita le idee con il loro ID storico (I-xxx); la corrispondenza con i temi è in fondo a §9.1.

| ID | Decisione | Motivazione | Origine |
|---|---|---|---|
| D-001 | Fase 1 solo **locale** (repo sulla stessa macchina); remote/hosting in fase 2. Il riferimento a una repo è sempre modellato come **URL git** (un path locale è un URL valido). Nota (I-016): per la fase 2 **non basta cambiare l'URL**; servono membership condivisa, refspec e un modello di pubblicazione diverso. | Il modello dati resta valido; il trasporto git (D-040) è già quello della fase 2. | I-003, I-016 |
| D-002 | Chi riceve un aggiornamento prende il **contenuto committato** (blob raggiungibile da un commit), mai il file nel WT altrui. | Riproducibilità: niente modifiche non committate che si propagano. | I-003 |
| D-003 | Nei dati versionati di una repo non va **nessun path locale**: solo ID, nome e path relativo del file shared e, in fase 2, gli URL remoti dei peer (portabili, D-050). I path locali delle altre repo stanno in `.git/config`, non versionato. | I path assoluti non sono portabili tra macchine, OS e clone. | I-001, I-003 |
| D-004 | Modello **paritetico**: ogni repo che usa il file può modificarlo liberamente. Il "padre" è solo la repo che lo ha dichiarato per prima. Obiettivo: **contenuto identico** in tutte le repo, nessuna divergenza voluta. | Scelta dell'autore: i file shared devono restare uguali. | I-003 |
| D-005 | `git shared link` (ex `add`, vedi D-011) accetta il **path del file** nel WT della repo sorgente; il tool risale a repo e path relativo. | Modello mentale più semplice per l'utente. | I-003 |
| D-006 | ID del file shared: **128 bit casuali (UUID v4)** generati al `set`, in esadecimale. L'utente usa path o nome breve, mai l'ID. | Path e contenuto non identificano un file shared; nessun coordinamento necessario. | I-006 |
| D-007 | **Lo stato shared cambia solo con un commit.** Modifiche in WT o in staging (incluso un `git add` del file shared) non sono ufficiali: non si propagano e si annullano con `git restore`. **Nessun comando `git shared` committa di default**: `set`, `link`, `unlink`, `update` preparano WT e index; l'utente "approva" con il commit. Il commit resta separato e dedicato all'operazione shared. Opzioni: `-c` / `--commit` per committare subito con messaggio standard; `-m <msg>` implica il commit con messaggio personalizzato. | History completa e coerente; principio dello snapshot; l'utente approva ogni cambio di stato; operazioni reversibili finché non committate. | I-005 (rev. 2) |
| D-008 | `git shared update` applica la versione sul **branch attualmente in checkout** della repo in cui viene lanciato. (Rev. 1: la formulazione originale, "la propagazione aggiorna il file nelle repo coinvolte", è superata da D-015: ricevere non tocca WT né index.) | Scelta dell'autore, resa coerente con D-015. | I-004, I-015 |
| D-009 | **Non si pubblica da una base obsoleta.** Si pubblica **solo in fast-forward** (D-014). La base **non è salvata**: si ricava dal branch corrente (D-041), perché ogni branch ha la propria copia del file mentre `refs/shared/<id>` è unica per repo. | Evita la perdita silenziosa di modifiche (scenari A, B, C di I-004). | I-004, I-007, I-015 |
| D-010 | Al checkout di un branch con versione shared non aggiornata: **avviso** tramite hook `post-checkout`; l'utente lancia `git shared update`. Nessun commit automatico. | Un commit automatico a ogni cambio di branch è invasivo. | I-004 |
| D-011 | Comandi rinominati: `git shared add` → **`git shared link`**, `git shared remove` → **`git shared unlink`**. | Evitare confusione con `git add` / `git rm`. | — |
| D-012 | **Nessuno store centrale.** Le repo che usano un file shared si conoscono tra loro come *peer*, sul modello dei remote git. Ogni repo tiene in `.git/config` (locale) una sezione `[shared "<id>"]` con l'**elenco completo** dei peer (rev. 1, D-068: nome e path non sono più copiati qui, stanno solo in `.gitshared`). | Restare dentro il modello di git (repo autonome, collegamenti come i remote); un orchestratore esterno snaturerebbe l'estensione. | I-004, I-007 |
| D-013 | Ogni repo ha la propria **`refs/shared/<id>`**, che punta a una catena di commit dedicata al file shared. Ogni commit ha un tree con un'unica voce a nome fisso (D-042). **Nessun path locale** nei commit della catena (D-003): chi ha creato la versione è identificato da autore e data del commit. | I blob sono già nell'ODB della repo; costo aggiuntivo trascurabile. I commit della catena sono replicati in tutti i peer, quindi non devono contenere dati locali. | I-007, I-015 |
| D-014 | L'ordine tra versioni si ricava dall'**ascendenza** (`git merge-base --is-ancestor`), senza contatori. Versioni non in relazione di ascendenza sono **divergenti** e si risolvono con un merge. | È la regola fast-forward di git: nessuna modifica persa, nemmeno da repo irraggiungibili. | I-007 |
| D-015 | **Ricevere ≠ applicare.** Ricevere = ottenere oggetti e ref `refs/shared/<id>` (via push di un peer o fetch, D-040); non tocca WT né index, sempre possibile. Applicare = `git shared update`, che porta la versione in WT e index; l'utente committa (D-007). | Le repo "non pronte" ricevono comunque la versione; l'utente decide quando applicarla. | I-004, I-007 |
| D-016 | Fase 1: propagazione in **broadcast** (chi pubblica aggiorna direttamente tutti i peer raggiungibili). L'anello resta un'opzione per la fase 2. | In locale l'anello non porta vantaggi; il broadcast è più semplice. | I-007 |
| D-017 | Accesso a git esclusivamente tramite **comandi git eseguiti come processi figli**: plumbing per oggetti, index, ref e config; `git fetch` e `git push --porcelain` per il trasporto (D-040). Niente libgit2. L'accesso è isolato in un modulo dedicato, sostituibile. | Zero dipendenze di runtime (§2.3), sempre allineato al git installato. | I-015 |
| D-018 | Fase 1: nel tree e nel WT c'è il **file vero**; il file shared è dichiarato in `.gitshared` (D-050; rev. 1: prima era una riga in `.gitattributes`). La variante **puntatore + clean/smudge** è prevista come evoluzione: l'architettura isola la "rappresentazione" del file shared per poterla sostituire. | Semplicità; repo utilizzabile anche senza l'estensione (es. compilare gli header). | I-002 |
| D-019 | `link` su un file non ancora shared nella sorgente → **errore**. | Il `set` è un atto esplicito del proprietario della sorgente. | REQ-F-002 |
| D-020 | **Superata da D-059.** In origine: le modifiche alla membership (`link`, `unlink`) valevano subito, perché la membership non è versionata. Resta valida solo per i comandi che toccano dati locali (`branch`, `prune`, `relocate`). | Coerente con D-003/D-012; `status` segnala incoerenze (es. `link` annullato con `git restore`). | REQ-F-002/003 |
| D-021 | *(Rev. 3, D-074: nessun hook sui commit, né `pre-commit` né `post-commit`. Rev. 2, D-073: niente `pre-commit`; il `post-commit` solo con `shared.autoPublish`.)* Controllo in due fasi: hook **`pre-commit`** applica la regola H/I/T (D-039) con soli controlli locali e **avvisa** sui commit da base obsoleta (rev. 1, D-058: prima li bloccava); hook **`post-commit`** pubblica. Se gli hook sono saltati o non scattano (`--no-verify`, merge, pull, rebase, cherry-pick), lo stato resta ricavabile (D-041) e si recupera con `status` / `publish` / `update`. | Segnala prima di creare il commit senza far dipendere la correttezza dagli hook. | REQ-F-006, I-015 |
| D-022 | `update` con modifiche locali non committate → **rifiuta**; `-f` / `--force` sovrascrive. | Mai perdere lavoro dell'utente senza un atto esplicito. | REQ-F-010 |
| D-023 | Riannuncio di una repo spostata con **comando esplicito** (`relocate`). Il *rilevamento* dello spostamento è invece automatico (D-035). | L'utente decide quando avvisare i peer; il rilevamento è un solo confronto di stringhe. | REQ-F-015 |
| D-024 | Fase 1: Linux e Windows. macOS rimandato a una release futura. | Priorità dell'autore. | REQ-NF-003 |
| D-025 | Messaggi in **inglese** di default, set alternativo in **italiano**, selezionabile. | Convenzione git + preferenza dell'autore. | REQ-NF-008 |
| D-026 | Fase 1: solo **file di testo**. Binari in futuro. | Il merge è definito solo per il testo. | REQ-NF-010 |
| D-027 | Fase 1: solo **file singoli**. Directory come estensione futura. | Semplicità; estensione naturale se il modello regge. | §5.4 |
| D-028 | `link`: path di destinazione di default = stesso path relativo della sorgente; path diverso ammesso come secondo argomento. | Layout diversi tra repo; il path è dato locale, costo nullo. | I-009 |
| D-029 | `link` su destinazione esistente con contenuto diverso e **senza base ricavabile** (rev. 1: prima si prova a riusare la storia, D-062): **errore** di default; `-f` sovrascrive con la versione shared; `--merge` scrive il file con marker di conflitto (base vuota) da risolvere prima del commit. | Il merge a 3 vie non ha base comune; mai perdere contenuto senza atto esplicito. | I-009 |
| D-030 | Hook: con git ≥ 2.54 **hook da configurazione** (`hook.<nome>.command` / `.event`); con git più vecchi file di hook con **blocco delimitato da marker**, mai sovrascrittura. La logica sta nell'eseguibile (`git shared hook <evento>`). | Coesistenza con hook esistenti e `core.hooksPath`. | I-010 |
| D-031 | Standard **C11**, sottoinsieme: `_Static_assert`, struct/union anonime, `_Alignas`/`_Alignof`, `_Noreturn`. Vietati: VLA, `<threads.h>`, Annex K. | Utile per lo stile §8; supportato da GCC, Clang, MSVC ≥ VS2019 16.8; coperto da MISRA C:2023. | I-008 |
| D-032 | Versione minima di git: **2.34** (Ubuntu 22.04 LTS). Matrice di test: 2.34 + ultima stabile, Linux e Windows (rivista da D-090). | Tutti i plumbing previsti sono disponibili; versione LTS diffusa. | I-011 |
| D-033 | Supporto **SHA-1 e SHA-256** previsto nel design (oid fino a 64 hex + algoritmo). `link` tra repo con algoritmi diversi → errore. | Costo basso se previsto da subito. | I-013 |
| D-034 | Limiti statici (rev. 1): path e URL **4096** caratteri (come `PATH_MAX` di Linux e git); **32 peer** per file shared; **nessun limite** al numero di file shared per repo (elaborazione di un elemento alla volta). Limiti e gestione delle stringhe isolati in un unico modulo. Valori iniziali, superati: path 128, 10 peer, 2 file shared. | Stile §8.3. Il limite a 128 era troppo stretto per path Windows e URL; l'elaborazione in streaming elimina il limite sul numero di file senza allocazione dinamica. | REQ-NF-011 |
| D-035 | `relocate`: l'utente indica il **vecchio path** (`git shared relocate <vecchio-path>`); `--peer <path>` per indicare a mano il nuovo path di un peer spostato. **Indizi all'utente**: ogni repo salva il proprio path in `.git/config` (`shared.selfPath`); a ogni comando `git shared` lo confronta con la posizione attuale (un confronto di stringhe, costo nullo) e, se differisce, suggerisce il comando già compilato (`git shared relocate /x/B`). Anche un peer irraggiungibile durante `publish`/`status` produce il suggerimento. | Semplice, nessun ID di repo; il vecchio path è ricordato dalla repo stessa, non dall'utente. | I-014 |
| D-036 | Obiettivi di prestazione (SSD, test di sistema): `status` (solo locale) < 100 ms su Linux, < 300 ms su Windows; pubblicazione verso 32 peer < 1 s; memoria (statico + stack) < 1 MiB. Obiettivo di miglioramento continuo oltre queste soglie. | Requisiti misurabili per il V-cycle. | I-012 |
| D-037 | Buffer di riga per l'output dei comandi git: **limite path + 256** caratteri (rev. 1; prima 256 fissi). Verifica a compile-time (`_Static_assert`) che sia ≥ della riga più lunga attesa (es. `ls-files -s`: modo + oid 64 + stage + path). | Coerenza tra limiti; nessun troncamento silenzioso (REQ-NF-011). | REQ-NF-011 |
| D-038 | Documentazione utente integrata: `-h` (sintassi rapida, gestita dall'eseguibile) e `--help` / `git help shared` (manuale completo: man page su Linux, HTML su Windows), più `git shared help` come manuale incorporato sempre disponibile. | La documentazione consultabile dal terminale è parte del prodotto; `--help` è intercettato da git e richiede man/HTML installati. | REQ-F-018/019 |
| D-039 | **Regola H/I/T** per il `pre-commit` (H = blob in HEAD, I = nell'index, T = ultima versione): I = H passa; I = T passa senza pubblicare (è il commit di un update); H = T passa e pubblica; merge shared in corso passa; altrimenti avvisa e non pubblica (rev. 1, D-058: prima bloccava). Solo controlli locali: i peer li verifica il push. | La regola precedente ("blocca se HEAD ≠ ultima versione") bloccava anche il commit di `update`. Interrogare i peer a ogni commit costa troppo, soprattutto su Windows. | I-015 |
| D-040 | **Trasporto git.** Pubblicare = `git push <peer> refs/shared/<id>:refs/shared/<id>`; `link` e la prima fase di `update` = `git fetch <peer> refs/shared/<id>` in ref temporanee. | Push e fetch trasferiscono anche gli oggetti e applicano da soli la regola fast-forward; `update` può ottenere le versioni divergenti dei peer; meno codice; stesso meccanismo in fase 2. | I-015 |
| D-041 | **Stato sempre ricavabile.** Nessuno stato shared è salvato: si calcola da H, I, T e dalla catena. La **base** di una modifica si ricava dalla storia del file sul branch corrente (rev. 1, D-065: dalla posizione nella storia delle versioni della catena; prima era la versione più recente il cui blob compare ovunque nella storia). Gli hook sono solo comodità. | La ref è per repo ma il file è per branch; gli hook non coprono merge, pull, rebase, cherry-pick. | I-015 |
| D-042 | Nel tree della catena il file ha un **nome fisso** uguale per tutti (`content`), indipendente dal path locale. Applicazione con `git update-index --cacheinfo` + `git checkout-index`. | I path differiscono tra repo (D-028); il contenuto non passa mai dai buffer dell'estensione (§8.3). | I-015 |
| D-043 | `link` funziona anche come **ri-aggancio**: se la repo ha già la sezione in `.gitshared` con quell'ID, ricrea membership, ref e hook senza errore. | Un clone conserva solo `.gitshared`; membership, hook e `refs/shared/*` non passano con `git clone`. | I-015 |
| D-044 | *(Rev. 3, D-074: superata, `shared.autoPublish` non esiste più.)* Pubblicazione automatica **configurabile**: `shared.autoPublish` (rev. 2, D-073: default `false`; prima `true`). (Rev. 1: `shared.publishBranch`, con default "tutti i branch", è sostituito dall'elenco dei branch attivi, D-055.) | Un commit su un branch sperimentale non deve per forza raggiungere tutti i peer. | I-015 |
| D-045 | **Rinomina**: `git shared mv`, effetto solo locale. **Cancellazione** del file in una repo: non si propaga; `status` la segnala e propone `unlink`. | Casi prima non definiti; cancellare un file in tutti i progetti deve essere un atto esplicito in ciascuno. | I-015 |
| D-046 | I confronti di contenuto si fanno sempre tra **oid di blob**, mai tra file del WT. | `core.autocrlf` / `eol` diversi tra repo darebbero falsi "modificato". | I-015 |
| D-047 | Nessun trailer con il path della repo d'origine nei commit della catena (vedi D-013). | Violava D-003: un path assoluto locale replicato in tutti i peer. | I-015 |
| D-048 | **Fase 2 minima: ricostruzione a mano.** Dopo un clone l'utente ricollega i file shared con `git shared link` (D-043); la membership resta locale. Notifica su tre livelli: la presenza stessa di `.gitshared` (con un commento in testa), stato *scollegato* in `status`, avviso dopo il clone tramite hook `post-checkout` globale installato da `git shared install` (git ≥ 2.54). | Nessuna modifica al modello della fase 1; il segnale (`.gitshared`) viaggia già con il clone. | I-016 |
| D-049 | **Versione seme deterministica.** Se `link` non trova la catena (né in locale né su `origin`), la ricrea con un commit dai campi fissi derivati dall'ID e il blob in HEAD: stesso contenuto → stesso commit in ogni clone. Contenuto diverso → caso D-029. Quando il clone incontra la catena vera, il seme viene sostituito (D-060). | `refs/shared/<id>` non viene clonata; senza seme due clone identici risulterebbero divergenti. | I-016 |
| D-050 | **File `.gitshared`** versionato alla radice della repo, già dalla fase 1, in formato config di git (`git config -f .gitshared`): sezione `[shared "<nome>"]` con `id`, `path` e, in fase 2, `url` dei peer remoti. Sostituisce la riga in `.gitattributes`. `.git/config` tiene solo i dati locali (path dei peer locali). L'elenco degli `url` può essere incompleto: chi non è in elenco non riceve il push ma ottiene le versioni con `update`. | File nuovo che solo l'estensione tocca; nessun parser da scrivere; nessuna interferenza con `.gitattributes`; la fase 2 aggiunge solo le righe `url`. Stesso schema di `.gitmodules` + `.git/config`. | I-017 |
| D-051 | **Fase 2:** il broadcast verso i peer remoti invia **solo la catena** `refs/shared/<id>`, mai commit sui branch altrui. Applicare resta `update` + commit in un clone (D-015, D-007). Peer senza permesso di scrittura: saltato, si aggiorna in pull. | Il broadcast notifica che esiste una versione nuova; l'approvazione resta all'utente; i branch protetti non sono toccati. | I-017 |
| D-052 | **Fase 2:** verso i remoti si pubblica **al `git push`** (hook `pre-push`), non al commit. Il commit crea la versione in locale (e la invia ai peer locali, D-016). | Niente rete a ogni commit; non si pubblica una versione il cui commit di progetto non è stato inviato. | I-017 |
| D-053 | **Fase 2: ordine fisso di push.** Prima fetch dai peer; poi push verso i peer in ordine fisso (URL ordinati), fermandosi al primo rifiuto. Chi è rifiutato fa `update` (merge) e ripubblica. | Il primo peer dell'ordine fa da arbitro senza ruolo speciale (D-004): di due pubblicazioni contemporanee una sola passa; le divergenze residue convergono con il merge. Caso particolare accettato: se i due non hanno lo stesso primo peer (elenchi di URL diversi, lo stesso peer scritto in due forme, permessi diversi) possono passare entrambe. Il risultato è una divergenza, che il merge risolve senza perdite. | I-017 |
| D-054 | **Prima la base, poi lo stato.** Lo stato di un file shared si ricava in due passi: (1) la base B secondo D-041; (2) il confronto di H con T e con il blob di B. *Da applicare* = B più vecchia di T e H = blob di B; *da pubblicare* = B = T e H ≠ T; *divergente* = B più vecchia di T e H ≠ blob di B, oppure nessuna base. L'appartenenza di H alla catena non è più un criterio. | La regola precedente ("H è il blob di una versione precedente → da applicare") scambiava un revert non pubblicato per una copia rimasta indietro: `publish` non faceva nulla e `update` lo sovrascriveva in silenzio, contro D-009. Lo stesso contenuto può comparire in più punti della storia: conta da dove parte la modifica, non a cosa somiglia. | reportIdea B-01 |
| D-055 | **Branch attivi, in locale.** Ogni file shared ha un elenco di branch su cui è gestito come shared, salvato in `.git/config` (`[shared "<id>"]`, voci `branch`). Al `set`/`link` contiene il solo branch corrente; si modifica con `git shared branch <file> --add/--remove`. Sugli altri branch e con HEAD staccata il file è **sospeso**: nessun blocco, nessun avviso, nessuna pubblicazione. `update` e `publish` espliciti restano possibili. Al rientro con un merge su un branch attivo lo stato si ricava con D-054. Voci come nomi o pattern (D-056); elenco iniziale copiato dal default versionato (D-057). | La catena è una per repo, il file è uno per branch: senza elenco ogni commit di prova diventava l'ultima versione per tutti i peer, e un branch di rilascio non poteva restare su una versione vecchia. È l'utente a decidere dove il file è shared. In locale e non in `.gitshared`: un dato versionato si fonderebbe tra branch con i merge, e i nomi dei branch sono locali. Elenco di ammessi e non di esclusi: ogni branch nuovo nasce libero. | reportIdea B-02, B-03 |
| D-056 | **Pattern nei branch attivi.** Una voce dell'elenco è un nome esatto o un pattern con i jolly di git (es. `release/*`). Il confronto è delegato a git (`git branch --list`). | Evita di aggiungere a mano ogni branch di una famiglia. Nessun codice di confronto da scrivere e stessa semantica che l'utente già conosce da git. | A-10 |
| D-057 | **Default versionato dei branch attivi.** La sezione del file in `.gitshared` può contenere voci `branch`. `set` vi scrive il branch corrente; si modifica con `git shared branch --default`. L'elenco locale è una **copia** fatta al `set`, al `link` e al ri-aggancio; dopo vale solo l'elenco locale, riallineabile con `--reset`. | Dopo un clone la repo riparte dalla scelta di chi l'ha configurata, invece che dal branch in cui capita di lanciare `link`. Copia e non lettura continua: il default è versionato e cambia con checkout e merge, quindi non può decidere il comportamento del branch corrente (motivo di D-055). Stesso schema di `.gitmodules` + `.git/config`. | A-11 |
| D-058 | **Il `pre-commit` avvisa, non blocca.** Un commit che modifica un file shared da una base obsoleta passa con un avviso e non viene pubblicato; il file risulta *divergente* e `git shared update` unisce. Nessuna opzione di forzatura sul commit: non serve, perché nulla è bloccato. Il `-f` resta su `update`, per sovrascrivere modifiche non committate (D-022). | Il blocco portava in un vicolo cieco: commit fermo, `update` che rifiuta per le modifiche locali. Non aggiungeva sicurezza, già data dal fast-forward (D-009) e dallo stato ricavato (D-041, D-054). L'utente decide e controlla il flusso. | reportIdea B-04 |
| D-059 | **Gli effetti di `set`, `link` e `unlink` partono al commit.** Al comando si prepara solo la repo corrente (WT, index, `.gitshared` in staging, hook, operazione in attesa registrata in `.git/config`). Ref, membership e annuncio ai peer avvengono al commit che conferma `.gitshared`. Prima del commit, `git restore` annulla tutto. Sostituisce D-020. | Coerenza con D-007: lo stato shared cambia solo con un commit. Un `link` annullato non lascia più la repo iscritta presso i peer. Il commit non è un costo in più: serve comunque per salvare `.gitshared`. L'operazione in attesa è uno stato salvato, eccezione dichiarata a D-041 come il merge in corso. | reportIdea B-06 |
| D-060 | **Il seme cede alla catena vera.** Se la catena locale è fatta del solo seme (D-049) e un peer ha una catena con una versione dallo stesso blob, la ref locale viene sostituita con quella del peer. | Il seme non discende dalla catena vera: senza questa regola due storie con contenuto identico risultavano divergenti e senza base. Il seme non contiene informazione, quindi sostituirlo non perde nulla. Limite: due semi con contenuto diverso restano senza base (D-029). | reportIdea B-08 |
| D-061 | **Fase 2: si pubblica dal branch attivo, anche dalla CI.** Il push di un branch sospeso (es. quello di una pull request) non pubblica nulla (D-055). Quando la modifica arriva su un branch attivo con un merge fatto sul server, dove gli hook non ci sono, la versione risulta *da pubblicare*: la pubblica `git shared publish`, lanciato da chi aggiorna quel branch o da un job di CI. `publish` deve quindi funzionare senza interazione e con codici di uscita affidabili. | La versione esce solo dopo revisione e merge. Dipende da A-01. | reportIdea B-10 |
| D-062 | **`link` riusa la storia esistente.** Se nella destinazione il file c'è già, `link` non lo tocca e ricava la base come per ogni file shared: blob uguale all'ultima versione → *allineato*; uguale a una versione precedente → *da applicare*; diverso, ma con una versione della catena nella storia del file → *divergente* con base vera. Solo senza base vale D-029. | Il caso d'uso di partenza sono copie fatte a mano: quasi sempre derivano da una versione comune. Riusare ciò che c'è evita il conflitto sull'intero file. Non serve codice nuovo: è la regola di D-054 applicata al collegamento. | reportIdea §4.5 |
| D-063 | **Il C non è una scelta assoluta.** Il nucleo resta in C11 (D-031), ma parti del sistema possono essere script `.sh` o `.bat`. La ripartizione si decide in architettura (§6), con il vincolo di REQ-NF-002. | Lanciare processi e leggerne l'output è la parte più scomoda in C e la più naturale per uno script. L'estensione non linka git, quindi "git è scritto in C" non è un vincolo. | reportIdea §4.9 |
| D-064 | **`link` su un file già shared con un altro ID → errore**; `--adopt` adotta l'ID della sorgente. L'adozione è un `unlink` della vecchia identità più un `link` della nuova, in un'unica operazione in attesa confermata dal commit (D-059). Il file non viene toccato: la base si ricava come in D-062; senza base vale D-029. | Il caso d'uso di partenza (copie fatte a mano) porta facilmente a un `set` in più repo: senza regola lo stesso path avrebbe due ID, due catene e push doppi. L'adozione è un atto esplicito perché fa uscire la repo dai peer della vecchia identità. | reportIdea B-19 |
| D-065 | **Base dalla posizione nella storia** (rev. di D-041; rev. 1, 2026-11-15). La storia del file sul branch si legge dalla più vecchia alla più recente: a ogni blob che è una versione della catena, la base diventa la versione **più vecchia** con quel blob che non sia più vecchia della base attuale; un blob presente solo in versioni più vecchie della base è una regressione (es. un revert) e la base non cambia. Se la versione più recente che compare nella storia è T, la base è T e ci si ferma subito. A un commit di merge in cui il file differisce da entrambi i genitori, ogni genitore dà una base: se sono diverse, il file è *divergente*. Nel dubbio lo stato è *divergente*, mai *da pubblicare*. Si calcola solo se H ≠ T, con un solo `git log` sul path; è ammessa una cache in `.git/` con chiave (HEAD, T), perché ricalcolabile. *Rev. 1:* la prima stesura ("prima corrispondenza risalendo da HEAD") dava base v1 a un revert da v2 ad A = v1, quindi *da applicare*: riapriva B-01. | Con la regola di D-041 ("la versione più recente il cui blob compare ovunque nella storia") un revert pubblicato o un `merge -s ours` facevano risultare *da pubblicare* una modifica da base vecchia, che poi cancellava una versione in tutti i peer. La regola per posizione tiene insieme i tre casi: revert non pubblicato → *da pubblicare*, `reset --hard` a una versione vecchia → *da applicare*, modifica dopo un revert pubblicato (B-14) → *divergente*. Un falso *divergente* costa un merge, un falso *da pubblicare* perde dati. | reportIdea B-14, B-28 |
| D-066 | **Allocazione dinamica ammessa** (rev. di D-034 e §8.3). `malloc` si usa dove un limite statico sarebbe artificiale (versioni della catena, elenchi, output dei comandi git). Un'allocazione fallita termina il programma con un messaggio e un codice d'errore, come `xmalloc` di git; la memoria non va liberata prima dell'uscita. L'allocazione resta isolata nel modulo di limiti e stringhe. La soglia di memoria di D-036 comprende l'heap. Gli altri limiti di D-034 si rivedono in architettura. | I comandi girano in locale e durano pochi millisecondi: frammentazione e tempi non deterministici, motivo del divieto MISRA, non contano. I limiti statici costringevano a inventare tetti, come il numero di versioni della catena per D-065. | reportIdea B-14 |
| D-067 | **Un unico eseguibile** `git-shared` con una tabella interna dei sottocomandi. Le differenze tra piattaforme stanno nel modulo `proc`, che lancia i processi senza shell. | git non trova `.bat`/`.cmd`; è git a lanciare l'estensione, qualunque sia la shell dell'utente; un dispatcher `sh` costerebbe un processo in più a ogni chiamata, hook compresi, e più pezzi da tenere coerenti. Un dispatcher resta aggiungibile senza cambiare l'interfaccia. | A-31 |
| D-068 | **In `.git/config` solo dati per repo.** La sezione `[shared "<id>"]` contiene peer, branch attivi e operazione in attesa; nome e path stanno solo in `.gitshared` (rev. di D-012). | `.gitshared` è per branch e cambia con checkout e merge: una copia in config divergerebbe (A-21). | A-32 |
| D-069 | *(Rev. 1, D-074: resta solo l'uscita senza `./.gitshared`; il filtro sui path toccati serviva a `pre-commit` e `post-commit`, che non ci sono più.)* **Percorso rapido degli hook.** Senza `./.gitshared` l'hook esce senza lanciare processi. `pre-commit` e `post-commit` elencano i path toccati con un solo processo ed escono se nessuno compare nel testo di `.gitshared` (filtro con soli falsi positivi; la lettura vera resta `git config`). Il blocco negli hook chiama `git-shared` direttamente. | Gli hook girano a ogni commit, anche nei commit che non riguardano file shared. Misurato su Windows: un commit passa da 61 a 164 ms invece che a 207. | Risposta dell'autore a M3 |
| D-070 | *(Rev. 2, D-074: la regola unica vale per `status` e `publish`. Rev. 1, D-073: il `pre-commit` non c'è più; la regola unica vale per `status`, `publish` e `post-commit`.)* **Una sola regola per `pre-commit` e `status`.** Il `pre-commit` ricava lo stato che il file avrà dopo il commit con la regola di `status` (D-054, D-065), usando il blob nell'index come voce più recente della storia; avvisa solo se risulterà *divergente*. Sostituisce "H = T" della regola H/I/T (D-039) per decidere avviso e pubblicazione. | Con "H = T", dopo un commit non pubblicato (`autoPublish = false`, hook saltato) ogni commit successivo dava un falso avviso di base obsoleta. | A-20 |
| D-071 | **Ambiente degli hook.** Nel `pre-commit` l'index è quello di `GIT_INDEX_FILE`, che i processi figli ereditano. I comandi diretti a un'altra repo (configurazione dei peer) partono senza le variabili che legano git a una repo (`GIT_DIR`, `GIT_WORK_TREE`, `GIT_INDEX_FILE`, …: `PROC_CLEAN_ENV`). | Verificato: con `commit -a` il `pre-commit` riceve `GIT_INDEX_FILE=.git/index.lock`. Senza pulizia, un comando verso un peer userebbe la repo o l'index della repo corrente. | A-27 |
| D-073 | *(Rev. 1, D-074: tolta anche la pubblicazione al commit su richiesta.)* **In locale si pubblica quando lo decide l'utente; al commit solo su richiesta.** Di default nessun hook gira sui commit: si pubblica con `git shared publish`, e `set`/`link` si completano al primo comando `git shared`. Con `shared.autoPublish = true` (default `false`, rev. di D-044) `git shared hook install` aggiunge il `post-commit`, che completa, pubblica e avvisa delle divergenze subito dopo il commit. Il `pre-commit` è tolto in ogni caso: rivede D-021 e D-039; D-070 resta per il `post-commit` e per `status`. Restano sempre gli hook di avviso (`post-checkout`, `post-merge`, `post-rewrite`), che non girano sui commit. | Misurato su Windows: gli hook costavano ~50–60 ms ciascuno, quasi tutto avvio di processi (shell, `git-shared`, un `git`); un commit qualunque passava da ~60 a ~165 ms. Il `pre-commit`, dopo D-058, non bloccava più nulla: il suo avviso dato dopo il commit porta la stessa informazione. Pubblicare quando si decide chiude A-12 (amend, reset e squash non lasciano nei peer versioni già superate) e avvicina il locale al remoto, dove si pubblica al `git push` (D-052). L'automatismo resta per chi lo vuole, a costo noto. | Discussione sul costo degli hook (M3), reportIdea B-05 |
| D-072 | *(Superata da D-074: il `post-commit` non c'è più. Il principio "si pubblica con la T attuale, aggiornamento condizionato" vale per `publish`.)* **Il `post-commit` decide con la T attuale.** Lo stato si ricava al momento del `post-commit`, non si riusa quello del `pre-commit`; la nuova versione si scrive con un aggiornamento condizionato a T. Se un peer ha pubblicato nel frattempo, il file risulta divergente e non si pubblica. | Tra `pre-commit` e `post-commit` la ricezione può cambiare T (D-015): pubblicare con la T vista prima perderebbe la versione del peer. | A-22 (parte) |
| D-074 | **Si pubblica solo con `git shared publish`.** `shared.autoPublish` e il `post-commit` sono eliminati: nessun hook gira sui commit. `set`/`link` si completano al primo comando `git shared` dopo il commit (o subito con `-c`/`-m`). `git shared hook install` toglie i blocchi `pre-commit` e `post-commit` lasciati dalle versioni precedenti; un blocco rimasto non fa nulla. Una chiave `shared.autoPublish` rimasta in configurazione è ignorata. Rivede D-021, D-044, D-069, D-070, D-072, D-073. | La pubblicazione al commit portava casi difficili per un risparmio piccolo: un amend dopo la pubblicazione lascia una versione già superata nei peer (B-05, B-34), il commit che chiude un merge non veniva visto (reportIdea B-33), la configurazione era letta in modo diverso da git (C-03), il filtro sui path toccati falliva con path scritti con escape (C-07). Un solo modo di pubblicare: meno codice, meno stati da spiegare, nessun costo sul commit. In fase 2 si pubblica comunque a un evento esplicito (`git push`, D-052). | Decisione dell'autore dopo la ricerca di bug (reportIdea §10) |
| D-075 | **`-c`/`-m` committano solo i file dell'operazione.** `set -c`: solo `.gitshared`; `link -c`: `.gitshared` e il file; `update -c`: i file applicati. Si usa `git --literal-pathspecs commit --only -- <path>`: ciò che l'utente ha in staging per altri file resta fuori dal commit e in staging. Il commit fatto a mano dall'utente resta libero (D-007). | Il "commit dedicato" di D-007 diventa una garanzia per i commit fatti dall'estensione. Con i pathspec letterali un nome con caratteri jolly (`a[b].h`) è solo un nome. | reportIdea B-25 (A-29), C-06 |
| D-076 | **Stato *da committare* e modifiche locali ricavate dal contenuto.** Se H ≠ T e il blob nell'index è T, lo stato è *da committare* ("applied, to commit"): `update` l'ha applicata e manca il commit. `update` non riscrive niente e con `-c`/`-m` committa; `publish` rifiuta; gli hook di avviso lo segnalano. Per `update` è una modifica locale un working tree diverso dall'index, oppure un index diverso da HEAD e da ogni versione della catena: una versione applicata e non committata si sostituisce con l'ultima senza `-f`, una modifica in staging è protetta. Lo stato resta ricavato (D-041): nessun dato salvato, una domanda in più al `cat-file` solo quando H ≠ T. | Dopo un `update` senza commit, `status` diceva ancora *da applicare*; se arrivava un'altra versione, `update` rifiutava per "modifiche locali" che erano la versione scritta da lui, e l'unica uscita era `-f`, che cancella anche il lavoro vero. Il vecchio controllo (`diff --quiet HEAD`) non vedeva le modifiche in staging. | reportIdea B-26 (A-30), B-30 |
| D-077 | **Le attese si legano al branch del comando.** `set` e `link` registrano nell'attesa il branch corrente (`pendingBranch`) e il path del file (`pendingPath`, solo per i messaggi). Il commit di conferma si cerca su `refs/heads/<pendingBranch>`, poi in HEAD: l'attesa si completa da qualunque branch sia lanciato il primo comando `git shared`. Si scarta solo se si è su quel branch e la sezione manca dal commit e dal working tree (`git restore`); da un altro branch resta. Se il branch non esiste più, resta con un avviso che dice come toglierla. Con HEAD staccata al comando vale la regola precedente (solo HEAD). Lo switch non si blocca: quando c'è davvero da scegliere tra commit e annullamento (`.gitshared` in staging in conflitto con l'altro branch) lo rifiuta già git. | Dopo D-074 il completamento avviene al primo comando `git shared`, non più al commit. Lanciato da un branch senza la sezione, scartava un `set`/`link` già committato: il file restava *scollegato* senza un comando per uscirne, e per `link` i peer non lo sapevano mai. Bloccare lo switch non serve: in quel caso il commit è già fatto, git non ha un hook `pre-checkout`, e git shared non blocca mai git (D-058, D-074). `pendingPath` è un'eccezione temporanea a D-068: sparisce con l'attesa. | reportIdea B-29 |
| D-078 | **Unione delle divergenze: la versione di merge nasce prima del commit.** `update` unisce a 3 vie con il plumbing di git (stage 1/2/3 nell'index, `merge-index git-merge-one-file`). Un'unione pulita crea subito la versione nella catena: dopo, il file è *da committare* (D-076) e la regola della base non cambia. Divergenza del branch: versione con un genitore (T); divergenza tra versioni: due genitori, T per primo. Prima il branch, poi le versioni dei peer, una alla volta; al primo conflitto ci si ferma. Il conflitto è quello di git; unico dato salvato `refs/shared-merge/<id>`, i cui genitori sono le versioni che la risoluzione incorpora e che vale solo finché l'index lo conferma (conflitto aperto o I ≠ H). Si conclude con `git add` e un nuovo `update`; si abbandona con `git restore`. Niente `--continue` né `--abort` *(rev. D-093: c'è `update --abort`)*. Rivede REQ-F-011 (stato in `.git/`, due genitori sempre, versione creata al commit della risoluzione). | Creare la versione al commit richiedeva di riconoscere quel commit, cioè uno stato salvato da tenere valido (B-13) e un hook sui commit, tolti con D-074. Con la versione creata prima, il blob del risultato è nella catena quando lo si committa: la base ricavata dalla storia (D-065) torna T senza regole nuove. La ref vale solo se l'index la conferma, quindi un abbandono non lascia tracce da ripulire. Se T avanza durante la risoluzione, la conclusione è un'altra unione con base la T di partenza: nessun caso speciale (B-17). Le false divergenze (squash, amend dopo `publish`) si chiudono con un'unione pulita, perché il contenuto locale contiene già T. Provato con git 2.43 su Windows; il plumbing usato esiste dalla 2.34. | reportIdea B-13, B-17, B-18, B-34, B-42 (A-16, A-22, A-23) |
| D-079 | **Un'attesa non si scarta se un branch la dichiara; `set` riprende un file scollegato.** (1) Prima di scartare un'attesa di `set`/`link` si cerca l'ID nel `.gitshared` committato di tutti i branch locali: se uno lo dichiara, il completamento parte da lì. Completa D-077. (2) `set` su un file già dichiarato in `.gitshared` ma senza dati locali riusa ID e nome: registra di nuovo l'attesa, e se la sezione è già committata completa subito. Su un file già collegato `set` continua a rifiutare. | (1) D-077 copriva "comando su un branch, primo `git shared` da un altro", non il caso speculare: `set` su `main`, `switch -c feat`, commit, primo comando di nuovo su `main`; l'attesa veniva scartata e il file restava *scollegato* senza uscita. Un contatore dei branch che dichiarano il file, salvato in `.gitshared` o in `.git/config`, è stato scartato: git crea, cancella e unisce branch senza avvisare, quindi il numero salvato diventerebbe falso (D-041); lo stesso numero si ricava quando serve. Costo: un processo più una lettura per ogni `.gitshared` distinto, solo quando si sta per scartare. (2) `git stash` toglie la sezione dal working tree come `git restore`: i due casi non si distinguono, quindi lo scarto resta ma diventa reversibile con un comando. Limite: dopo un clone `set` crea una catena nuova, separata da quella dei peer; lì serve il ri-aggancio (D-043). | reportIdea B-39, B-40 |
| D-080 | **Ri-aggancio di `link` nella v0; push dei peer senza hook.** (1) `link` su un file dichiarato in `.gitshared` con lo stesso ID ma *scollegato* lo ricollega (D-043): non tocca il file, usa nome e path della sezione locale, rifà fetch, attesa e hook; se la sezione è già committata completa subito, altrimenti rimette in staging `.gitshared` (e il file se non è in HEAD) e completa al commit. Con `-c` committa solo quei path: le modifiche dell'utente a un file già in HEAD restano fuori. (2) `peers_push` usa `git push --no-verify`. | (1) Dopo D-079 `set` recuperava uno scarto, ma `link` no; e il caso del clone, il più comune, restava senza uscita: `set` lì crea una catena nuova e separata. Lo stato dopo il ri-aggancio si ricava come sempre dalla storia (D-041): un clone ha la stessa storia della sorgente, quindi entra *allineato* o *da applicare*. (2) Il push di `refs/shared/<id>` non è un push dell'utente: il suo `pre-push` (test, lint, git-lfs) girava una volta per peer e, se falliva, il peer risultava "non raggiungibile" con codice di uscita 0. | reportIdea B-40, C-20; D-043 |
| D-081 | **`unlink` nella v0.** Al comando: sezione tolta da `.gitshared` (il file sparisce se resta vuoto), staging, attesa con branch e commit del comando (`pendingCommit`). Conferma: HEAD o un branch locale che discende da `pendingCommit` e il cui `.gitshared` non dichiara l'ID. Al completamento i peer tolgono la repo dalla membership, poi ref e sezione locale. Annullamento con `git restore` sul branch del comando. Senza attesa: file scollegato (si toglie solo la sezione) e sezione locale orfana passata per ID, se nessun branch locale la dichiara. | Per `set` e `link` la conferma è la *presenza* della sezione in un commit; per `unlink` è l'*assenza*, che però vale anche per ogni branch nato prima del `set`: senza il commit del comando un branch vecchio avrebbe completato un unlink mai committato. Con `pendingCommit` il caso speculare di B-39 (unlink su `main`, commit su `feat`) si completa e un branch vecchio no. La pulizia per ID chiude B-45 senza un comando nuovo; il controllo sui branch evita di smontare un file ancora shared altrove (B-16). | REQ-F-003, reportIdea B-45 |
| D-082 | **`prune` nella v0.** Un peer si toglie solo se si vede che non c'è più: nessuna repo al suo path (*sparito*) o una repo senza la sezione del file (*uscito*). Se la repo c'è ma git non la legge, resta con un avviso. I peer tolti si tolgono anche dalla membership dei peer membri. Senza `<path>` si tolgono anche le ref di lavoro senza sezione locale. `-n` mostra soltanto. Effetto immediato. | La paura di B-07 era confondere "cancellato" con "non raggiungibile adesso": in locale la differenza si vede, perché un path che non esiste più è un fatto e un lock o un permesso no. Togliere il peer anche dagli altri realizza "su tutti i peer raggiungibili" di REQ-F-014 senza un passo di riconciliazione. Il caso *uscito* copre l'`unlink` non notificato a una repo irraggiungibile (B-07). Le ref orfane non hanno nessuno stato che le descriva (B-27): `prune` è il posto naturale per toglierle. | REQ-F-014, reportIdea B-07, B-27, C-22 |
| D-083 | **Comando `branch` nella v0.** Mostra elenco locale e default di ogni file (o di uno), con il branch corrente e i nomi che non corrispondono a nessun branch. `--add`/`--remove` cambiano l'elenco locale con effetto immediato; `--reset` lo ricopia dal default; `--default --add/--remove` cambia `.gitshared` (staging, `-c`/`-m` per committare). Solo nomi esatti. L'elenco locale non può restare vuoto. | I pattern (D-056) costerebbero un processo git a ogni `status` e a ogni hook per fare il confronto: restano fuori finché non servono davvero, e un pattern scritto oggi è rifiutato invece di non funzionare in silenzio. L'elenco vuoto è rifiutato perché lascerebbe vuota la sezione locale di un file senza peer, che git non distingue da una sezione assente; il caso d'uso ("sospeso ovunque") è coperto da `unlink`. Segnare i nomi inesistenti rende visibile la rinomina di un branch (B-21) senza un hook nuovo. | REQ-F-022, reportIdea B-21 |
| D-084 | **`relocate` nella v0, con rilevamento.** `shared.selfPath` si scrive al primo comando con un file collegato; ogni comando avvisa se la repo non è più lì (non nei worktree secondari). `relocate [<vecchio>]` aggiorna i peer; se al vecchio path c'è ancora una repo rifiuta e propone `--copy`, che fa della copia un peer in più. `--peer <vecchio> <nuovo>` corregge qui la voce di un peer spostato (due argomenti: con uno solo, non si sa quale voce sostituire). Il vecchio path si rende assoluto anche se non esiste più. `branch` accetta anche `-a` e `-rm`. | Spostamento e copia non si distinguono dal contenuto (B-22): li distingue il vecchio path, che dopo uno spostamento è vuoto. Rifiutare invece di indovinare evita di escludere dai peer una repo che esiste ancora; `--copy` rende la copia utile invece di lasciarla con l'identità della repo originale. Il path ricordato evita di dover digitare il vecchio path nella forma esatta. Un worktree secondario ha la stessa config della repo principale: un avviso lì sarebbe falso (reportIdea §7 punto 12). | REQ-F-015, REQ-F-017, reportIdea B-22 |
| D-085 | **Nessun silenzio.** (1) Un file sospeso il cui elenco di branch attivi non contiene **nessun** branch esistente è segnalato: riga `note` in `status`, avviso negli hook di checkout/merge/riscrittura, in `update` e `publish`, con il comando `git shared branch <file> -a <branch>`. I branch si leggono (un processo) solo se c'è un file sospeso. (2) `publish <file>` e `update <file>` su un file in attesa o scollegato danno un errore con il motivo (codice 1); senza argomento, una riga per ogni file saltato. | (1) Il caso tipico è la rinomina di un branch (B-21): il file smette di essere gestito senza che nulla lo dica. "Nessuno esiste" e non "uno non esiste": un branch aggiunto prima di crearlo è legittimo, e un avviso a ogni `status` sarebbe rumore. Niente hook `reference-transaction`: gira a ogni aggiornamento di ref, quindi a ogni commit, contro D-074. (2) Un comando che non fa nulla e risponde 0 sembra riuscito (B-37). | reportIdea B-21, B-37 |
| D-086 | **`status --peers`.** Per ogni file collegato e ogni peer: stato della repo come in `prune` (membro, uscito, sparito, illeggibile) e punta della sua `refs/shared/<id>`, letta senza fetch. Punta uguale a T: stessa versione; antenato di T (oggetto presente qui): più vecchia, `publish`; altrimenti più nuova o diversa, `update`; nessuna punta: nessuna versione, `publish`. Una riga per peer, con il comando da lanciare. | Dopo un'unione e il commit `status` dice *allineato* anche se i peer non hanno ancora la versione (reportLocal §3.3): serviva un modo per vederlo. Senza fetch il comando non tocca nulla, né qui né nei peer; la distinzione tra "più nuova" e "divergente" richiederebbe gli oggetti del peer, e il rimedio è lo stesso (`update`). | REQ-F-012 |
| D-087 | **Aiuto distribuito in `help/`.** Una pagina di testo semplice per comando e una pagina generale breve che rimanda alle altre. `git shared <comando> --help` e `git shared help <comando>` le stampano; le pagine sono incorporate nell'eseguibile al build da `tools/embed.c`, che ne ricava anche HTML e man page per `git shared --help`. Rivede REQ-F-019 e chiude A-06. | Proposta dell'autore: una documentazione distribuita, leggibile così com'è nel repository, senza XML né strumenti come AsciiDoc. Git intercetta solo `git shared --help`: `git shared <comando> --help` arriva all'eseguibile (provato), quindi ogni comando può avere la sua pagina come i comandi di git. Incorporare evita di dover trovare `help/` a runtime. L'HTML con un `<pre>` basta a Git for Windows, che apre il file nel browser (provato con un browser finto). | Proposta dell'autore, A-06 |
| D-088 | **Manuale anche in italiano; `lang` nella v0.** Traduzione di tutte le pagine in `help/it/`, incorporate come le inglesi con la lingua ricavata dalla cartella. `git shared lang [en|it]` legge o scrive `shared.lang` (config globale); `help` e `<comando> --help` scelgono la pagina in quella lingua, con l'inglese come riserva. `git shared --help` apre la pagina installata da `make install` nella lingua scelta in quel momento. `git shared -h` spiega i quattro modi di aprire il manuale e il comando per l'italiano. I messaggi dei comandi restano in inglese. | Richiesta dell'autore. La lingua del manuale si può cambiare subito senza toccare i messaggi: tradurre `msg.c` è un lavoro a parte (REQ-NF-008). Per `git shared --help` la pagina sta in una cartella di git, che su Windows richiede i permessi di amministratore: non la può riscrivere `lang`, quindi si rigenera con `make install`. Il breef diceva che `lang` sceglieva la lingua dei messaggi, ma il comando non esisteva. | Richiesta dell'autore, REQ-NF-008 |
| D-089 | **Il modo di un hook esistente non cambia.** Inserire o togliere il blocco riscrive il file sul posto e ne lascia i permessi come sono; il bit di esecuzione si mette solo all'hook che git shared crea. Un hook non eseguibile resta disattivato, e con lui il blocco. | Trovato provando Linux (reportIdea C-25): il codice rendeva eseguibile ogni hook che toccava, quindi `set` riattivava un hook che l'utente aveva disattivato con `chmod -x`, e aggiungeva l'esecuzione anche per gruppo e altri. Rispetta "mai sovrascrivere" (D-030). Gli hook sono solo comodità (D-041): un blocco che non gira non cambia lo stato. Su Windows il bit non esiste e git esegue ogni hook. | reportIdea C-25, D-030 |
| D-090 | **Compatibilità dichiarata chiusa, con una riga della matrice non provata.** Provati: Windows 11 con git 2.43 e 2.56.1, Linux (WSL2, Ubuntu 22.04) con git 2.34 e 2.56.1; 832 test su 832 in tutti e quattro i casi. git 2.34 su Windows non è provata ed è ritenuta stabile; si prova se servirà. Rivede la matrice di D-032 senza cambiare il minimo (REQ-NF-004). | Decisione dell'autore. I comandi git usati sono gli stessi sulle due piattaforme e con 2.34 passano su Linux; il codice proprio di Windows non dipende dalla versione di git; su Windows sono provate una versione intermedia (2.43) e l'ultima. Il rischio che resta è un difetto di Git for Windows 2.34 che i test non vedono altrove. | Decisione dell'autore, D-032 |
| D-091 | **La catena è un grafo: le versioni sono tutte quelle raggiungibili da T.** L'elenco delle versioni usato per la base (D-065) e per le modifiche locali (D-076) comprende entrambi i genitori delle unioni (D-078). Ordine: ogni versione prima dei suoi genitori; tra versioni nessuna delle quali discende dall'altra, la linea dei primi genitori prima di quella dei secondi. La regola della base non cambia. Si visita il grafo in profondità sullo stesso `cat-file` già aperto (tre domande per versione): nessun processo nuovo. | reportIdea B-46: seguendo il solo primo genitore la versione di un peer, entrata come secondo genitore in un'unione fatta da un'altra repo, non era una versione per nessuno. Il peer la vedeva *divergente* con base la versione comune più vecchia, e il suo `update` rimetteva in silenzio ciò che l'altra repo aveva tolto dopo l'unione (provato). Viola §2.3 punto 5 e la regola 5 del breef. Un `git rev-list --topo-order` avrebbe dato lo stesso elenco con un processo in più a ogni stato non banale. | reportIdea B-46 |
| D-092 | **Unioni tra versioni: "chi contiene chi" lo dice git.** (1) Se T non è tra le versioni ricevute rimaste da `merge-base --independent`, T avanza sulla versione che la contiene: l'unica rimasta, oppure la prima per cui `merge-base --is-ancestor T <versione>` è vero (l'ultima non si chiede). (2) `refs/shared-merge/<id>` scaduta la toglie il primo comando che ricava lo stato, non solo `update`; il manuale e il messaggio del conflitto dicono di lanciare `git shared status` subito dopo il `git restore`. (3) Nel ciclo delle unioni di `update` una versione del peer che T contiene già si salta. Rivede D-078 (abbandono) e completa REQ-NF-005. | (1) reportIdea B-47: T avanzava su `tops[0]`, la prima nell'ordine dei peer, anche quando non la conteneva: un avanzamento non fast-forward, e con un conflitto la versione locale restava fuori dalla catena. (2) B-48: dopo l'abbandono `status` diceva *allineato* ma la ref restava, e la prima modifica in staging diventava la risoluzione: una versione che dichiarava di contenere quella del peer senza le sue righe. Dallo stato della repo un abbandono seguito da una modifica in staging non si distingue da una risoluzione (anche `git restore` registra le resolve-undo come `git add`, provato): resta scoperto solo il caso senza nessun comando `git shared` in mezzo, che il manuale indica come evitare. Un `update --abort` lo chiude del tutto: fatto con D-093. (3) B-49: la conclusione di un conflitto tra versioni creava una seconda versione con lo stesso contenuto, perché la versione del peer restava nell'elenco da unire. | reportIdea B-47, B-48, B-49 |
| D-093 | **`git shared update --abort [<path>]`.** Rinuncia all'unione in corso: per ogni file scelto con `refs/shared-merge/<id>`, il blob di HEAD torna nell'index (gli stage di conflitto spariscono) e nel working tree, poi la ref si cancella. Nessun fetch, T non cambia. Senza unione: errore; con `-c`, `-m`, `-f`: uso. Diventa la procedura di abbandono nel messaggio del conflitto, nel manuale e nel breef; `git restore` resta tollerato con le regole di D-092. Rivede D-078 ("niente `--abort`") e completa A-16. | reportIdea B-48: con `git restore` l'abbandono è fatto da git, e dallo stato non si distingue da una risoluzione finché un comando `git shared` non lo vede (D-092). Un comando dedicato fa le due cose insieme, quindi non c'è più un intervallo in cui una modifica in staging passi per la risoluzione. Costa un'opzione, non un comando nuovo: sta in `update`, che ha creato l'unione, come `git merge --abort`. Si è scartato un hook `post-index-change`, che vedrebbe anche il `git restore` a mano: girerebbe a ogni scrittura dell'index, commit compresi, contro D-074. | reportIdea B-48, A-16 |
| D-094 | **Lo stato non dipende dalla configurazione git dell'utente.** I due comandi porcelain che concorrono allo stato fissano sulla riga di comando ciò che la configurazione può cambiare. `git log --raw` di `find_base`: `--root` (contro `log.showRoot=false`), `--no-follow` (contro `log.follow=true`, che segue le rinomine anche con `--no-renames`), `--diff-merges=off` (contro `log.diffMerges`), `--no-show-signature` (contro `log.showSignature`). `git diff --quiet` del controllo delle modifiche: `-c diff.autoRefreshIndex=true`. Gli altri comandi sono plumbing, oppure la configurazione dell'utente vale per scelta: `git commit` (è il suo commit), `checkout -m` (`merge.conflictStyle`, solo la forma dei marker). Si resta su `git log` invece di `rev-list` + `diff-tree --stdin`: un processo invece di due in catena, con lettura interrotta appena possibile. | reportIdea B-32. Provato con git 2.34 e 2.43: `log.showRoot=false` toglie il commit radice dalla storia (file nato nel primo commit: *divergente*, `publish` rifiuta); `log.follow=true` porta nella storia i contenuti del file col nome di prima (lo stato diventa *da pubblicare* e `publish` scriverebbe sopra una versione più nuova); `diff.autoRefreshIndex=false` fa sembrare modificato un file solo toccato (`update` rifiuta). `log.diffMerges` e `color.ui` non cambiano l'output, `diff.external` e textconv non contano con `--quiet`: fissati comunque dove costa zero, per non dipendere da come git li tratterà. | reportIdea B-32 |
| D-095 | **Sezione locale non dichiarata sul branch: il file non è shared qui.** Con la sezione `[shared "<id>"]` in `.git/config` e nessuna sezione con quell'ID nel `.gitshared` del working tree: se HEAD la dichiara, `.gitshared` è stato toccato a mano e il file è *incoerente*; se no, e un altro branch locale la dichiara, il branch corrente è nato prima del `set` (o il `set` non vi è mai arrivato) e il file vi è *sospeso* (`not declared here`), con una nota che dice su quale branch è shared e il path che ha lì; se nessun branch la dichiara la sezione è orfana e resta *incoerente* (`unlink <id>`, D-081). Hook, `update` e `publish` già non toccavano queste voci. Il controllo costa un `for-each-ref` e la lettura dei `.gitshared` distinti, solo in questo caso. | reportIdea B-16 (A-21): su un branch nato prima del `set` `status` diceva *incoerente* e il breef suggeriva `unlink`, che lì non ha senso. Il caso speculare, un branch vecchio che dichiara ancora un file tolto con `unlink`, risulta *scollegato* come dopo un clone: dallo stato non si distingue, e resta aperto come reportIdea B-52. | reportIdea B-16, A-21 |
| D-096 | **Il blocco entra solo negli hook di shell.** Un hook che esiste già riceve il blocco solo se è uno script di shell: senza riga `#!` (git lo esegue con `sh`), oppure con un interprete `sh`, `bash`, `dash`, `zsh`, `ksh`, `mksh`, `ash`, anche dopo `env` (opzioni escluse) o `busybox`, con o senza `.exe`. Un binario (un byte nullo nel file) o un altro interprete (Python, Perl, ...) non si tocca, e un avviso dice che da lì non arriveranno gli avvisi di git shared. Un blocco che una versione precedente aveva messo in un hook così si toglie, e l'hook torna com'era. | reportIdea C-02: il blocco è codice sh; dopo la prima riga di un hook Python lo rendeva invalido, e un hook binario veniva troncato al primo byte nullo dalla riscrittura. Gli hook sono una comodità (D-041): senza blocco mancano solo gli avvisi, che `status` dà comunque. Un blocco scritto nella lingua di ogni interprete sarebbe una lista senza fine. | reportIdea C-02 |
| D-097 | **Variabili d'ambiente in UTF-8.** `sys_getenv` restituisce il valore di una variabile in UTF-8, come ogni path del programma: su Windows lo legge in UTF-16 (`_wgetenv`) e lo converte, su POSIX lo copia. `git_program` legge così `GIT_EXEC_PATH` e controlla `git.exe` con `sys_fopen`. | reportIdea C-05: `getenv` su Windows dà il valore nella code page ANSI (1252 su questa macchina); il programma lo trattava come UTF-8, e con git sotto una cartella come `C:\Users\José` ogni comando usciva con "cannot run 'git'" (codice 128). Provato con una junction `git-José` verso la exec-path vera. | reportIdea C-05 |
| D-098 | **La suite si lancia solo con l'ultima git stabile.** Dal 2026-11-17 `make test` gira con git 2.56.1, su Windows (PortableGit) e su Linux (WSL2); non più con 2.34 e 2.43. Rivede l'uso della matrice di D-032 e D-090, non il minimo dichiarato (REQ-NF-004). | Decisione dell'autore, che se ne assume la responsabilità: i comandi git usati sono gli stessi già provati con le versioni vecchie fino a D-096, e le correzioni successive non ne aggiungono di nuovi che il minimo non abbia (le opzioni nuove di D-094 sono provate anche con 2.34). | Decisione dell'autore, D-032, D-090 |

### 9.1 Percorso di progettazione
Questa sezione spiega **perché** il progetto è fatto così. È organizzata per temi, nell'ordine in cui conviene leggerli. Ogni tema riporta la domanda, le alternative valutate e l'esito, con le decisioni corrispondenti. Le idee ancora aperte sono raccolte in §9.2.

#### T-01 – Come si identifica un file shared
- **Domanda.** Serve un ID univoco? Della repo o del file?
- **Alternative.**
  - *ID di repo.* Git non ne ha uno nativo. L'hash del primo commit è condiviso dai fork e cambia se si riscrive la storia; un UUID in `git config` non passa con il clone.
  - *Path o contenuto del file.* Il path può differire tra repo e cambiare con un rename; il contenuto cambia a ogni modifica.
  - *ID del singolo file shared.* Generato una volta, indipendente da repo, path e contenuto.
- **Esito.** ID del file: 128 bit casuali (UUID v4) generati al `set` (D-006). L'utente usa il path o il nome, mai l'ID. Un ID di repo è stato riconsiderato per `relocate` e scartato: basta il vecchio path, che la repo ricorda da sola (D-035).

#### T-02 – Come si dichiara che un file è shared
- **Domanda.** Dove si scrive che un file è shared? L'obiettivo iniziale era non avere un manifest separato.
- **Alternative.**

  | Opzione | Pro | Contro |
  |---|---|---|
  | Nuovo modo nel tree (es. `170000`) | Il file stesso è shared | Impossibile senza modificare git: `fsck` e gli hosting lo rifiutano (§3) |
  | Trailer nei commit | Nessun file in più | Si perde con shallow clone, squash, rebase |
  | git notes | Non tocca file né messaggi | Non viaggia con push/fetch di default |
  | Riga in `.gitattributes` | Meccanismo standard | Si modifica un file che appartiene anche ad altri usi |
  | File puntatore + filtri clean/smudge (come Git LFS) | Il file *è* il riferimento | Senza l'estensione la repo contiene solo puntatori: build, CI e web si rompono |
  | **File `.gitshared`** (come `.gitmodules`) | File nostro, formato config di git (nessun parser), viaggia con il clone, pronto per gli URL | È un manifest |

- **Esito.** File vero nel tree e nel working tree (D-018), dichiarato in `.gitshared` (D-050). Il manifest è stato accettato quando è emerso che serve comunque per il remoto e che un file dedicato è più pulito che toccare `.gitattributes`. Nei dati versionati non va nessun path locale (D-003).
- **Sul puntatore.** Resta una possibile evoluzione. Da solo non fa risparmiare spazio: il contenuto deve comunque stare nella repo per il checkout senza rete. Il risparmio di Git LFS viene dal tenere i contenuti fuori dalla repo.

#### T-03 – Ruoli delle repo
- **Domanda.** Esiste una repo "padre" proprietaria del file?
- **Alternative.** Padre con figli in sola lettura; figli che propongono modifiche al padre; repo alla pari.
- **Esito.** Modello paritetico: ogni repo può modificare il file; il padre è solo chi lo ha dichiarato per primo (D-004). `link` indica il file con il suo path nell'altra repo (D-005), che deve averlo già dichiarato (D-019). Il path di destinazione può differire (D-028). Se nella destinazione il file esiste già, `link` riusa la sua storia: se il contenuto è una versione della catena entra *allineato* o *da applicare*; se una versione compare nella storia entra *divergente* con una base vera (D-062). Solo quando manca una base comune resta errore, `-f` o `--merge` (D-029). Se la destinazione è già shared con un altro ID (es. `set` lanciato in entrambe le repo), `link` dà errore; `--adopt` adotta l'ID della sorgente (D-064).

#### T-04 – Quando cambia lo stato shared
- **Domanda.** Le operazioni shared producono un commit? Chi lo crea?
- **Percorso.** Prima ipotesi: ogni comando committa da sé. Obiezione dell'autore: un `git add` non è una modifica ufficiale, si può annullare con `git restore`; lo stesso deve valere per i comandi shared.
- **Esito.** Lo stato shared cambia solo con un commit, e il commit lo fa l'utente. Nessun comando `git shared` committa di default; `-c` e `-m` lo fanno su richiesta (D-007). Ne deriva che chi riceve un aggiornamento prende sempre contenuto committato (D-002). Vale anche verso le altre repo: `set`, `link` e `unlink` preparano la repo corrente, ma ref, membership e annuncio ai peer partono al commit; prima, `git restore` annulla tutto (D-059, che sostituisce D-020). Il commit non è un passo in più: serve comunque per salvare `.gitshared`.

#### T-05 – Dove sta la verità: topologia
- **Domanda.** Chi sa qual è l'ultima versione di un file shared, e chi conosce chi?
- **Alternative.**

  | Topologia | Sopravvive a cancellazioni | Trovare l'ultima versione | Giudizio |
  |---|---|---|---|
  | Hub nella repo padre | No: cancellato il padre, salta tutto | Banale | Scartata: contraddice T-03 |
  | Store centrale per utente (repo bare fuori dai progetti) | Sì per le repo; lo store è un punto singolo | Banale: una ref | Scartata dall'autore: esce dal concetto di repo, diventa un orchestratore |
  | Store minimale (solo ultima versione) o ibrido | Sì | Banale | Scartate per lo stesso motivo |
  | Catena: ogni repo conosce 2 vicine | 1 repo persa la spezza | Giro completo | Scartata: gli estremi spezzati non si conoscono |
  | Anello: 2 vicine, chiuso | 2 repo perse lo spezzano | Giro completo | Rimandata (D-016) |
  | **Peer: ogni repo conosce tutte le altre** | Sì, finché ne resta una | Dall'ascendenza (T-06) | **Scelta** |

- **Esito.** Nessuno store centrale: le repo si conoscono come peer, sul modello dei remote git (D-012). È il modello di git stesso, applicato a un file. Ogni repo tiene la propria `refs/shared/<id>` (D-013). In locale la propagazione è in broadcast (D-016).
- **Nota sul costo.** I blob delle versioni sono già nel database della repo, perché ogni versione è stata committata; la catena aggiunge solo un commit e un tree per versione.

#### T-06 – Versioni, ordine e conflitti
- **Domanda.** Come si evita che una modifica ne sovrascriva un'altra?
- **Il problema.** Con un solo utente non ci sono modifiche simultanee, ma c'è la **base obsoleta**: si modifica il file partendo da una versione non aggiornata.
  - *Repo saltata*: A pubblica; C non era pronta e resta indietro; più tardi si modifica il file in C e si pubblica: la modifica di A sparisce ovunque.
  - *Cambio di branch*: B applica l'aggiornamento su `feature`; si passa a `main`, dove il file è vecchio, lo si modifica e si pubblica.
  - *Storia riscritta*: `reset`, `revert` o checkout di un commit vecchio, poi commit.
- **Esito.**
  - Non si pubblica da una base obsoleta: solo fast-forward (D-009).
  - L'ordine tra versioni si ricava dall'ascendenza dei commit della catena, senza contatori; versioni non confrontabili sono divergenti e si uniscono con un merge (D-014).
  - Ricevere non è applicare: la ricezione tocca solo la ref, quindi è sempre possibile (D-015). `update` applica sul branch in checkout (D-008) e non sovrascrive modifiche locali senza `-f` (D-022).
- **Correzioni della revisione architetturale** (I-015). La prima stesura aveva questi difetti:

  | Difetto | Rimedio | Decisione |
  |---|---|---|
  | Il `pre-commit` bloccava anche il commit di `update` | Regola H/I/T | D-039 |
  | Una versione divergente non arrivava a chi doveva fare il merge | `update` fa prima `git fetch` dai peer | D-040 |
  | La ricezione aggiornava la ref ma non trasferiva gli oggetti | Trasporto con `git push` e `git fetch` | D-040 |
  | La base era salvata per repo, ma il file è per branch | Base ricavata dalla storia del branch | D-041 |
  | Un path locale finiva nei commit della catena, replicati ovunque | Trailer eliminato | D-047 |
  | I path differiscono tra repo | Nome fisso nel tree della catena | D-042 |
  | Fine riga diversi tra repo davano falsi "modificato" | Confronti solo tra blob | D-046 |

- **Correzione successiva** (reportIdea B-01). Lo stato si decideva guardando se H era il blob di una versione della catena. Un revert non pubblicato (contenuto uguale a una versione vecchia) risultava *da applicare* e `update` lo cancellava. Rimedio: prima si ricava la base, poi lo stato (D-054). Un revert ha base T, quindi è *da pubblicare*; un `reset --hard` a un commit vecchio ha base vecchia, quindi resta *da applicare*.
- **Dal blocco all'avviso** (reportIdea B-04). Il `pre-commit` bloccava i commit da base obsoleta e rimandava a `update`, che però rifiutava per le modifiche locali: un vicolo cieco. Ora avvisa e lascia passare; la modifica non viene pubblicata, il file risulta *divergente* e `update` unisce (D-058). La protezione resta quella del fast-forward.

#### T-07 – Automatismi e hook
- **Domanda.** Cosa avviene in automatico, e cosa succede se l'automatismo non scatta?
- **Esito.**
  - Nessun hook sui commit: si pubblica solo con `publish` (D-074). Prima: `pre-commit` che avvisava (D-058) e `post-commit` che pubblicava (D-021), tolti da D-073 e D-074. Il controllo dei peer lo fa il push (D-039).
  - Gli hook non coprono merge, pull, rebase, cherry-pick e `--no-verify`. Per questo lo **stato è sempre ricavabile** e gli hook sono solo comodità: `status`, `publish` e `update` recuperano (D-041).
  - Al cambio di branch si avvisa, non si modifica nulla (D-010).
  - Il file è shared solo sui **branch attivi**, scelti dall'utente e salvati in locale; altrove è sospeso e si modifica liberamente (D-055).
  - Gli hook esistenti non si sovrascrivono mai: hook da configurazione con git ≥ 2.54, altrimenti un blocco delimitato da marker (D-030).

#### T-08 – Repo cancellate, spostate, rinominate, clonate
- **Cancellata.** Le altre continuano; `prune` la toglie dalla membership.
- **Spostata.** Ogni repo ricorda il proprio path e si accorge dello spostamento; suggerisce `relocate <vecchio-path>`, che avvisa i peer (D-023, D-035). Se si spostano due repo insieme nessuna trova l'altra: `relocate --peer` indica a mano il nuovo path.
- **File rinominato o cancellato.** `git shared mv` rinomina, con effetto solo locale; la cancellazione non si propaga e `status` propone `unlink` (D-045).
- **Clonata.** Il clone conserva solo `.gitshared`: mancano membership, hook e catena. `link` funziona come ri-aggancio (D-043); se la catena non c'è, la ricrea con una **versione seme deterministica**: stesso contenuto, stesso commit in ogni clone (D-049). Quando il clone incontra la catena vera, il seme viene sostituito (D-060).

#### T-09 – Dal locale al remoto
- **Domanda.** Cosa cambia con GitHub o GitLab?
- **Differenze.**
  1. Le macchine degli sviluppatori non si raggiungono: i peer sono le repo sull'hosting.
  2. `.git/config` non passa con il clone.
  3. `refs/shared/*` non viaggia con clone, fetch e push ordinari.
  4. Pubblicare verso un peer richiede il permesso di scrittura.
  5. Sul server non ci sono i nostri hook (merge request fuse dal web).
  6. Con più utenti le divergenze sono normali.
- **Esito.**
  - La fase 1 è solo locale, ma ogni riferimento è già un URL git (D-001).
  - Dopo il clone ricollega l'utente, a mano; la presenza di `.gitshared` segnala che la repo ha file shared (D-048).
  - Gli URL dei peer remoti stanno in `.gitshared`; l'elenco può essere incompleto, perché chi non è in elenco si aggiorna in pull (D-050).
  - Il broadcast invia solo la catena, cioè la notizia di una versione nuova; mai commit sui branch altrui (D-051).
  - Verso i remoti si pubblica al `git push`, non al commit (D-052).
  - I conflitti non si possono impedire senza un centro: si rilevano (fast-forward) e si fanno convergere (merge). L'ordine fisso di push fa del primo peer un arbitro senza ruolo speciale (D-053). In casi particolari (elenchi di URL diversi, permessi diversi) due pubblicazioni contemporanee possono passare entrambe: nasce una divergenza, che il merge risolve.
  - Si pubblica solo dai branch attivi: il push del branch di una pull request non pubblica nulla. Dopo il merge sul server, dove gli hook non ci sono, pubblica `git shared publish`, lanciato a mano o dalla CI (D-061).

#### T-10 – Scelte tecniche
| Tema | Esito | Decisione |
|---|---|---|
| Accesso a git | Comandi git come processi figli (plumbing, `fetch`, `push`); niente libgit2. Le struct interne di git non sono un'API (§3) | D-017 |
| Linguaggio | Nucleo in C11, sottoinsieme: `_Static_assert`, struct/union anonime, `_Alignas`, `_Noreturn`. Script `.sh` / `.bat` ammessi; ripartizione da decidere in architettura | D-031, D-063 |
| Memoria | Allocazione dinamica ammessa, uscita se la memoria si esaurisce; un elemento alla volta dove basta; limiti: path 4096, 32 peer, nessun limite ai file shared né alle versioni | D-034, D-037, D-066 |
| Versione di git | Minimo 2.34; hook da configurazione dove c'è 2.54; compatibilità chiusa, 2.34 su Windows non provata | D-032, D-030, D-090 |
| Hash | SHA-1 e SHA-256 previsti nel design | D-033 |
| Prestazioni | Soglie misurabili; il costo dominante è l'avvio dei processi git, da ridurre con le modalità batch | D-036 |
| Piattaforme | Linux e Windows | D-024 |
| Contenuto | Solo file di testo, solo file singoli | D-026, D-027 |
| Interfaccia | Nomi `link`/`unlink`; messaggi in inglese e italiano; `-h` e manuale | D-011, D-025, D-038 |

#### Corrispondenza con gli ID storici
| ID | Titolo originale | Tema |
|---|---|---|
| I-001 | ID univoco di repo | T-01 |
| I-002 | Nessun manifest separato | T-02 |
| I-003 | Modello padre/figli, fase locale | T-03 |
| I-004 | Propagazione e conflitti | T-05, T-06 |
| I-005 | Commit per ogni operazione shared | T-04 |
| I-006 | Generazione dell'ID | T-01 |
| I-007 | Topologia decentralizzata | T-05 |
| I-008 | C99 o C11 | T-10 |
| I-009 | Path di destinazione di `link` | T-03 |
| I-010 | Coesistenza con hook esistenti | T-07 |
| I-011 | Versione minima di git | T-10 |
| I-012 | Prestazioni misurabili | T-10 |
| I-013 | SHA-256 | T-10 |
| I-014 | `relocate` e identità della repo | T-08 |
| I-015 | Revisione architetturale | T-06, T-07, T-08 |
| I-016 | Cosa manca per GitHub / GitLab | T-09 |
| I-017 | `.gitshared` e broadcast verso gli URL | T-02, T-09 |

### 9.2 Idee aperte
Non ancora decise. Ognuna diventa una decisione o viene scartata prima di entrare nei requisiti.

| ID | Idea | Stato |
|---|---|---|
| A-01 | **Hosting e ref personalizzate.** Verificare che GitHub e GitLab accettino il push di `refs/shared/*` e con quale ruolo minimo. Verificare anche se protezioni dei branch e regole coprono `refs/shared/*`, e cosa succede con `--force` e `--mirror` (A-28). Condiziona tutta la fase 2. | Da verificare |
| A-02 | **Membership dentro la catena.** Il tree di ogni versione contiene anche l'elenco dei peer, che così si replica da solo. Alternativa all'elenco di `url` in `.gitshared`, che richiede un commit in ogni repo per ogni nuovo peer. | Da valutare per la fase 2 |
| A-03 | **`git shared verify` per la CI.** Esce con errore se un file shared è divergente o modificato da base obsoleta. Sostituisce il `pre-commit` dove gli hook non arrivano (merge request). La pubblicazione dalla CI è già decisa (D-061). | Da valutare per la fase 2 |
| A-04 | **`git shared install`.** Configurazione una volta per macchina: hook `post-checkout` globale che avvisa dopo un clone (D-048), refspec per `refs/shared/*`. | Da definire |
| A-05 | **Scoperta delle novità in remoto.** Fetch esplicito (`git shared fetch`) o automatico dopo `git pull`. | Da valutare per la fase 2 |
| A-06 | **Formato del manuale.** Sorgente unico da cui generare man page, HTML e testo incorporato. | Deciso: D-087 (testo semplice in `help/`) |
| A-07 | **Variante puntatore** (clean/smudge), utile solo se i contenuti stanno fuori dalla repo (file grandi). | Evoluzione futura |
| A-08 | **Propagazione ad anello** al posto del broadcast. | Evoluzione futura |
| A-09 | **Directory, file binari, macOS.** | Evoluzioni future |
| A-17 | **Licenza.** | Ultima cosa da decidere, a progetto terminato e funzionante |
| A-18 | **Ripartizione tra C e script** (`.sh`, `.bat`): cosa fa il programma C e cosa gli script (D-063). | Da decidere in architettura (§6) |
| A-13 | **Riconciliazione della membership.** Se un peer non è raggiungibile durante `link`/`unlink`, gli elenchi restano diversi; `prune` non distingue "cancellato" da "non raggiungibile ora" (reportIdea B-07). Un peer che non riceve la notifica di `unlink` continua a inviare la ref, che ricompare orfana senza che nessuno stato la descriva (reportIdea B-27). Collegata ad A-02. | In parte: D-082 (`prune` distingue sparito da irraggiungibile e toglie le ref orfane); manca l'ingresso di peer nuovi visti da un altro peer |
| A-14 | **Commit della catena deterministici**, perché la stessa modifica fatta in due repo non risulti divergente (reportIdea B-09). | Da valutare |
| A-15 | **Fine riga e bit di esecuzione** uguali tra i peer: controllo al `link` o contenuto sempre normalizzato nella catena (reportIdea B-12). | Da valutare |
| A-16 | **`git shared update --abort`** e controllo di validità del merge in corso (reportIdea B-13). | Deciso: D-078 (validità), D-093 (`--abort`) |
| A-21 | **`.gitshared` per branch, membership per repo** (reportIdea B-16). Sui branch nati prima del `set`, o dopo un `unlink`, lo stato è *incoerente* o *scollegato* e il comando suggerito (`unlink`, `link`) è dannoso. Proposta: sezione assente sul branch corrente = file non presente qui, trattato come sospeso. | Deciso: D-095 per i branch nati prima del `set`; resta il caso dopo un `unlink` (reportIdea B-52) |
| A-22 | **T che avanza durante un merge** (reportIdea B-17). Lo stato del merge registra T all'inizio; se al commit è cambiata, niente pubblicazione e nuovo `update`. (Il caso tra `pre-commit` e `post-commit` è deciso: D-072.) | Deciso: D-078 |
| A-23 | **Due divergenze nello stesso `update`** (reportIdea B-18): catena divergente tra peer e branch divergente insieme. Proposta: prima la catena, poi il branch; al più un merge per `update`. Collegata ad A-16. | Deciso: D-078 |
| A-24 | **Collisione di nomi in `.gitshared`** (reportIdea B-20). Sezione per nome, nome di default = nome del file: `a/config.h` e `b/config.h` finiscono nella stessa sezione. Chiave per ID o controllo di unicità. | Da valutare |
| A-25 | **Elenco dei branch attivi senza corrispondenze** (reportIdea B-21). Dopo `git branch -m` il file diventa sospeso senza avvisi. Proposta: `status` e hook (anche `reference-transaction`, git ≥ 2.28) segnalano le voci che non corrispondono a nessun branch esistente. | Deciso: D-083, D-085 (senza `reference-transaction`) |
| A-26 | **Spostamento o copia** (reportIdea B-22). `relocate` dopo una copia esclude dai peer la repo originale; path Windows scritti in forme diverse danno falsi spostamenti e peer doppi. `relocate` verifica che il vecchio path non esista più; i path si normalizzano prima di salvarli e confrontarli. | Deciso: D-084 |
| A-28 | **Fast-forward garantito solo dal client** (reportIdea B-24). `--force`, `--mirror` (verificato: cancella `refs/shared/*` sulla destinazione) e `--prune` lo scavalcano. Il rifiuto può essere "fetch first" oltre a "non-fast-forward": `publish` deve riconoscerli entrambi. Collegata ad A-01. | Da valutare per la fase 2 |
| A-29 | **Commit dedicato non garantito** (reportIdea B-25). `-c`/`-m` devono committare solo il file shared e `.gitshared` (`git commit --only`); il commit manuale dopo `update` può contenere altro. | Deciso: D-075 |
| A-30 | **Stato "applicato, da committare"** (reportIdea B-26). Dopo `update` senza commit, `status` dice ancora *da applicare* e un secondo `update` rifiuta per "modifiche locali" che sono la versione precedente. | Deciso: D-076 |
| A-31 | **Notifica invece del push** (fase 1). `publish` scrive in ogni peer un file di avviso in `.git/shared/inbox/` invece di lanciare un `git push`; il peer riceve con un fetch al suo primo comando `git shared`. Toglie i processi per peer dalla pubblicazione. Scheda sotto. | Proposta dell'autore, da decidere prima di implementarla |

#### A-31 – Notifica invece del push (proposta, 2026-11-17)

**Problema.** Il costo di `publish` è l'avvio dei processi, non i dati. Misure del 2026-11-17 (Windows, git 2.56.1, 12 core, da Git Bash, che aggiunge il proprio costo a ogni lancio: per `git --version` §6.1 misura 30 ms, qui 65): `publish` verso 10 peer 710–820 ms, 300 ms senza niente di nuovo; un push verso un peer 116 ms (due processi, `git` e `receive-pack`); 10 push in parallelo 330 ms; 10 `update-ref` in parallelo 220 ms. Anche con un solo processo per peer il costo resta O(N); la soglia di REQ-NF-007 (32 peer < 1 s) è a rischio. Per scalare servono zero processi per peer.

**Proposta.** In locale il broadcast (D-016) invia una notifica, non la catena. Il trasferimento lo fa chi riceve, quando gli serve.
- **Invio (`publish`).** La versione nasce come oggi (`mktree`, `commit-tree`, `update-ref` condizionato). Poi, per ogni peer, senza processi: si scrive `<peer>/.git/shared/inbox/<id>/<oid di T>` in un file temporaneo nella stessa cartella e lo si rinomina (atomico). Contenuto: una riga, la radice della repo che pubblica, come la scrive `rev-parse --show-toplevel`. Se il file esiste già non si riscrive. L'esito per peer è *notificato* o *irraggiungibile* (REQ-F-008: cartella `.git` assente o non scrivibile).
- **Ricezione.** Il primo comando `git shared` che ricava lo stato (`status`, `update`, `publish`) legge la cartella degli avvisi dei file collegati: una lettura di directory, nessun processo. Gli avvisi con un nome che non è un oid della lunghezza giusta (REQ-NF-009) o con un ID senza sezione locale si scartano.
  - Un oid che T contiene già si cancella senza fetch.
  - Per ogni repo annunciante che è tra i peer: un fetch, come il primo passo di `update` (D-040, `refs/shared-incoming/`), poi l'avanzamento di `receive` (D-092). Un fetch per repo, non per avviso.
  - Una repo annunciante che non è tra i peer (spostata prima di `relocate`, path scritto in un'altra forma): fetch da tutti i peer, come `update` oggi.
  - L'avviso si cancella quando T contiene la versione. Una versione ricevuta ma divergente lascia l'avviso al suo posto: lo stato del file lo segnala e `update` unisce (REQ-F-011). Un fetch fallito lascia l'avviso per il comando successivo.
- **Ripiego sul push.** Se `<peer>/.git` non è una cartella (worktree secondario, `--separate-git-dir`, reportIdea B-50), si pubblica con il push come oggi.

**Che cosa non cambia.** La catena, l'ordine per ascendenza, la regola fast-forward e "ricevere ≠ applicare" (D-013, D-014, D-015). La ref di una repo la scrive solo la repo stessa, con `update-ref` condizionato. La ricezione non tocca WT e index. In fase 2 il push della ref resta la notifica: D-051 lo descrive già così.

**L'avviso vive solo in locale e non è mai versionato** (autore, 2026-11-17). Sta dentro `.git`, quindi non entra in nessun commit, non viaggia con clone, push o fetch, e non compare in `.gitshared`. Locale e remoto possono notificare in modi diversi, a una condizione: condividono tutto il modello, cioè catena `refs/shared/<id>`, ID, `.gitshared`, regola dello stato e ref avanzata solo in fast-forward dalla repo stessa. Così una repo con peer locali e remoti insieme (D-050) può ricevere una versione dall'avviso e inviarla con il push: è lo stesso oggetto della catena.

**Verifica delle collisioni** (decisioni, requisiti e bug in `project.md`, `reportIdea.md`, `reportLocal.md`, `breef.md`):

| Riferimento | Che cosa tocca | Come si risolve |
|---|---|---|
| §2.3 p. 3, D-001, D-040 ("stesso meccanismo in fase 2") | In locale la pubblicazione non usa più il push | Il modello è lo stesso; cambia solo il canale della notifica, che in fase 2 è il push stesso (D-051). Va riscritta la frase: "stesso modello, notifica diversa". **Accettato dall'autore** (2026-11-17): locale e remoto possono seguire strade separate, perché non sono correlati |
| §2.3 p. 8, D-041 (stato sempre ricavabile) | Terzo dato salvato, dopo l'attesa (D-059) e l'unione in corso (D-078) | L'avviso non entra nello stato: dice solo dove fare il fetch. Se si perde, il peer riceve al suo prossimo `update`, come oggi un peer irraggiungibile. Va aggiunto all'elenco delle eccezioni come "suggerimento eliminabile" |
| §5.0 p. 4–5, D-015, REQ-F-007 (a) | Il peer riceve con il proprio fetch, non con il push di chi pubblica | Ricevere resta separato da applicare. Tra la notifica e il primo comando nel peer la sua ref resta indietro, ma l'avviso è già lì |
| REQ-F-006, D-009 | Chi pubblica non riceve più il rifiuto "non-fast-forward" o "fetch first" | Il fast-forward lo verifica chi riceve (`receive`, D-092). Due pubblicazioni contemporanee arrivano entrambe e la divergenza la unisce `update` nel peer, senza perdite (REQ-F-011). Il breef ("ne passa una sola") parla della fase 2, D-053: non cambia |
| REQ-F-008 | Che cosa vuol dire "irraggiungibile" | Avviso non scrivibile. Un peer con la configurazione rotta risulta notificato, e lo dirà il suo comando successivo |
| REQ-F-012, REQ-NF-007 (`status` solo locale) | `status` fa un fetch quando trova avvisi | La soglia di `status` vale senza novità; con avvisi costa un fetch per repo annunciante. Alternativa: `status` mostra solo una nota (punti aperti) |
| D-086 (`status --peers`) | Un peer notificato ma non ancora ricevuto risulterebbe "più vecchio: `publish`" | Se `<peer>/.git/shared/inbox/<id>/<T>` esiste: "annunciata, non ancora ricevuta", niente da fare |
| D-082 (`prune`), reportIdea B-27 | Avvisi rimasti per ID senza sezione | Anche `prune` li toglie (non quelli di un'attesa). Effetto utile: un peer che ha fatto `unlink` non si ritrova più la ref ricreata da un push altrui (resto di B-27); gli avvisi con un ID sconosciuto si ignorano |
| D-084 (`relocate`) | L'avviso porta il path di chi pubblica | Un path che non è tra i peer dà il fetch da tutti. Una copia (`cp -r`) copia anche gli avvisi: sono suggerimenti, senza effetti |
| REQ-NF-005 | Atomicità | Avviso: file temporaneo e rinomina. Ref: solo `update-ref` condizionato nella repo stessa |
| REQ-NF-006, §5.3, D-017 (solo comandi git) | Scrittura di un file dentro il `.git` di un'altra repo | Non è un file di git né un accesso a git: `.git/shared/` è dell'estensione (precedente: `.git/shared/base-cache`, §6.3). Oggetti e ref passano ancora solo da comandi git. Va scritto in REQ-NF-006 |
| §6.3 (file config solo con `git config`) | Formato dell'avviso | Non è un file config: il nome è l'oid, il contenuto una sola riga confrontata per uguaglianza con i peer, ≤ 4096 caratteri (REQ-NF-011). Il contenuto del file shared non passa dai buffer |
| D-069, D-071 (hook) | Gli hook dovrebbero ricevere? | No: niente fetch negli hook, che girano dentro le operazioni di git con il loro ambiente. Leggono la cartella e aggiungono "nuova versione annunciata: run 'git shared update'" |
| D-080, reportIdea C-20 | `push --no-verify` | Resta nel ripiego |
| reportIdea A-28, B-24 | Fast-forward garantito solo dal client | Migliora: nel percorso normale nessuno scrive più le ref di un'altra repo |
| reportIdea B-35, B-51, C-26 | Versione creata prima del push; catena assente; lettura di T | Nessun conflitto. B-51: la ricezione può rifare la catena assente con lo stesso fetch, che è il rimedio proposto lì |

**Punti aperti.**
1. `status` che riceve (stato giusto, un fetch quando ci sono novità) oppure `status` che mostra solo una nota (sempre locale, ma *allineato* insieme a una nota). Proposta: riceve.
2. `update` fa oggi il fetch da tutti i peer, O(N) processi anche lui. Con gli avvisi potrebbe farlo solo dalle repo annuncianti, e da tutti con un'opzione. Da decidere con il punto 1.
3. Messaggi: `publish` dice "peers notified" ("`<file>`: N of M peers notified") invece di "sent to N of M peers". **Accettato dall'autore** (2026-11-18).
4. Test nuovi (**accettati dall'autore**, 2026-11-18): invio, ricezione da `status`/`update`/`publish`, avviso perso, avviso divergente, path sconosciuto, ID sconosciuto, ripiego con `.git` file, `status --peers`, `prune`. Misura di `publish` verso 32 peer prima e dopo (REQ-NF-007).

**Stima del guadagno** (non misurata): `publish` ≈ la parte locale (tre processi) più N scritture di file piccoli, sotto il millisecondo l'una; il costo di ricezione lo paga una volta per novità chi riceve. Scartati: `publish` in background (stesso lavoro totale, rifiuti visti in ritardo) e processi git sempre vivi (servirebbe un demone per repo, contro D-012).

**Se si decide**, la decisione (D-099) riscrive: §2.3 p. 3 e 8, §5.0 p. 4–5, REQ-F-004, REQ-F-007, REQ-F-008, REQ-F-012 (`--peers`), REQ-F-014, REQ-NF-006, §6.3 (riga e formato dell'avviso), §6.4 (`status`, `publish`), D-016 e D-040 (rev.), e incrementa la baseline.
