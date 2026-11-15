# Git shared file

## 1. Scopo del progetto

L'obiettivo è realizzare un'estensione git per supportare una feature attualmente non presente in git: i file condivisi.

L'idea è quella di utilizzare file in maniera condivisa da più progetti (repo) git e, che, su ogni progetto, si possano aggiornare tali files in maniera semi-automatica (stile submodules) richiamando il comando dedicato di aggiornamento. In questo modo è sufficiente modificare un file su un singolo progetto git per riportare le modifiche su tutti i progetti che fanno uso del file condiviso.
Uno degli esempi più eclatanti è quello degli header condivisi tra più progetti. Aggiornarli tutti ogni volta è una gran fatica ed è facile introdurre bug tra i vari aggiornamenti manuali.
l'aggiornamento di un file shared deve produrre sempre un nuovo commit per essere in linea con la filosofia git dello snapshot. Infatti, deve essere sempre possibile il versionamento conologicamente univoco della singola repo.
> Nota (D-007): il commit di aggiornamento è dedicato all'operazione shared ed è confermato dall'utente (o creato subito con `--commit`).

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
8. **Stato sempre ricavabile.** Lo stato di un file shared si calcola dal contenuto della repo, non da dati salvati: hook saltati, merge, rebase, cloni e spostamenti non lo corrompono (D-041).
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
- **Plumbing e porcelain.** I comandi plumbing hanno output stabile e sono fatti per i programmi; l'output dei comandi porcelain può cambiare.
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
> **BASELINE v2.10** (2026-11-15) — derivata dalle decisioni D-001…D-068. v2.10: base ricavata dalla posizione nella storia (D-065 rev. 1). v2.9: unico eseguibile (§6.2, D-067); in `.git/config` solo dati per repo (REQ-F-020, D-068). v2.8: base ricavata dalla prima corrispondenza nella storia (REQ-F-012, D-065); allocazione dinamica ammessa (REQ-NF-011, §8.3, D-066). v2.7: `link` su una destinazione già shared con un altro ID (REQ-F-002, D-064). v2.6: il `pre-commit` avvisa e non blocca (REQ-F-006, D-058); effetti di `set`/`link`/`unlink` al commit (REQ-F-023, D-059); seme sostituito dalla catena vera (D-060); `link` riusa la storia esistente (REQ-F-002, D-062); C non esclusivo (REQ-NF-001, D-063). v2.5: pattern e default versionato dei branch attivi (REQ-F-021, REQ-F-022, D-056, D-057). v2.4: branch attivi per file shared (REQ-F-022, D-055). v2.3: stato ricavato prima dalla base, poi dal confronto (REQ-F-012, D-054). v2.1: `.gitshared` al posto di `.gitattributes` (REQ-F-021). v2.2: nuovi limiti statici (REQ-NF-011). v1.1: documentazione utente (REQ-F-018, REQ-F-019). v2.0: revisione architetturale I-015 (trasporto git, stato ricavabile, regola H/I/T, rinomina e cancellazione, pubblicazione configurabile). Ogni modifica successiva passa dal registro decisioni (§9) e incrementa la versione.
> Formato: ogni requisito ha un ID univoco, un testo verificabile e l'origine. La verifica è tracciata in §7.

### 5.0 Il modello in sintesi
Da leggere prima dei requisiti. I termini sono definiti nel glossario (§4), le motivazioni in §9.

1. **Dichiarazione.** `git shared set <file>` rende shared un file della repo. Il file riceve un ID casuale ed è registrato in `.gitshared`, versionato alla radice della repo.
2. **Collegamento.** In un'altra repo, `git shared link <path-del-file>` collega lo stesso file. Al commit che conferma il collegamento le due repo diventano **peer** e ognuna registra l'altra (D-059). Tutte le repo sono alla pari. Se nella repo il file esiste già, `link` riusa la sua storia invece di sovrascriverlo (D-062).
3. **Versioni.** Ogni repo ha la ref `refs/shared/<id>`: una catena di commit che contiene solo il file shared. È la storia delle versioni del file, uguale in tutti i peer, separata dalla storia del progetto.
4. **Modifica e pubblicazione.** Il file si modifica e si committa come ogni altro. Se la modifica parte dall'ultima versione, il commit crea una nuova versione nella catena e la invia ai peer con `git push` della ref.
5. **Ricezione.** Nel peer cambia solo `refs/shared/<id>`. File, index e branch non sono toccati.
6. **Applicazione.** `git shared update` porta l'ultima versione nel working tree e nell'index; l'utente conferma con un commit.
7. **Protezione.** Si pubblica solo in fast-forward. Un commit che modifica il file partendo da una versione vecchia passa con un avviso ma non viene pubblicato (D-058); due versioni indipendenti sono *divergenti* e `update` le unisce con un merge a 3 vie.
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
- **REQ-F-001 – `set`.** `git shared set <path>` dichiara shared un file tracciato della repo corrente. **Al comando**: genera l'ID (D-006); crea o aggiorna `.gitshared` con la sezione del file (nome, `id`, `path`, `branch`) e lo mette in staging (D-050). **Al commit di conferma** (REQ-F-023): crea `refs/shared/<id>` con la prima versione, presa dal blob del file in quel commit (D-002, D-013, D-042), e la sezione locale `[shared "<id>"]` in `.git/config` (D-012). Non committa, salvo `-c`/`--commit` o `-m <msg>` (D-007). *Origine:* I-003, D-006, D-007, D-012, D-013, D-018.
- **REQ-F-021 – File `.gitshared`.** Ogni repo con file shared ha alla radice un file versionato `.gitshared`, in formato config di git, con una sezione per file shared: `[shared "<nome>"]` con `id`, `path` e zero o più `branch` (default dei branch attivi, D-057) in fase 1, più zero o più `url` di peer remoti (fase 2). Il file è letto e scritto solo dall'estensione, tramite `git config -f .gitshared`. Nessun path locale assoluto (D-003, D-050).
- **REQ-F-002 – `link`.** `git shared link <path-sorgente> [<path-destinazione>]` collega alla repo corrente un file shared di un'altra repo, indicato con il suo path nel WT della sorgente (D-005). Il tool risale a repo sorgente, path relativo e ID; ottiene la catena con `git fetch <sorgente> refs/shared/<id>` (D-040); scrive il file nel WT e nell'index a partire dal blob (D-042); aggiorna `.gitshared` e lo mette in staging (D-050); **al commit di conferma** (REQ-F-023) scrive in `.git/config` la membership completa (peer della sorgente + la sorgente) e si annuncia a tutti i peer, che lo aggiungono alla loro membership. *Origine:* D-002, D-005, D-012, D-040, D-064.
  - Path di destinazione: default = stesso path relativo della sorgente; path diverso ammesso come secondo argomento (D-028).
  - Se il file nella sorgente non è ancora shared → **errore** (D-019).
  - **Destinazione già esistente: si riusa la storia** (D-062). `link` non tocca il file e ricava la base come per ogni file shared (D-041, D-054):
    - il blob locale è quello dell'ultima versione → entra *allineato*;
    - il blob locale è quello di una versione precedente → entra *da applicare*: `update` lo porta all'ultima versione senza conflitti;
    - il blob locale non è nella catena, ma una versione della catena compare nella storia del file → entra *divergente* con una base vera: `update` fa un merge a 3 vie;
    - nessuna versione compare nella storia → nessuna base: errore; `-f` sovrascrive; `--merge` scrive il file con marker di conflitto (D-029).
  - **Ri-aggancio** (D-043): se la repo corrente ha già in `.gitshared` la sezione con lo stesso ID (es. dopo un clone), `link` non dà errore: ricrea membership, ref e hook senza toccare il file.
  - **Destinazione già shared con un altro ID** (D-064): se il path di destinazione è già dichiarato in `.gitshared` con un ID diverso da quello della sorgente, `link` dà **errore**. Con `--adopt` la repo adotta l'ID della sorgente: in un'unica operazione in attesa (REQ-F-023) la vecchia identità viene scollegata come con `unlink` (REQ-F-003) e il file viene collegato con il nuovo ID. Il file non viene toccato e la base si ricava come per una destinazione esistente (D-062); senza base vale D-029. Al commit i peer del vecchio ID ricevono la notifica di `unlink`, quelli del nuovo l'annuncio di `link`.
  - **Seme** (D-049, D-060): se la catena non si trova, `link` la ricrea con una versione seme deterministica. Quando più tardi la repo ottiene da un peer una catena che contiene una versione con lo stesso blob del seme, e la catena locale è fatta del solo seme, la ref locale viene **sostituita** con quella del peer. Il seme non porta informazione, quindi non si perde nulla. Se sul seme sono già state pubblicate versioni, o i blob non coincidono, vale la regola della divergenza (REQ-F-011).
- **REQ-F-003 – `unlink`.** `git shared unlink <path>` toglie il file dalla gestione shared nella repo corrente: rimuove la sezione da `.gitshared` (staging); **al commit di conferma** (REQ-F-023) rimuove la sezione da `.git/config` e `refs/shared/<id>` e notifica i peer, che la rimuovono dalla membership. Il file resta nel WT come file normale. *Origine:* D-007, D-011.
- **REQ-F-023 – Conferma al commit.** `set`, `link` e `unlink` agiscono in due momenti (D-059):
  - **Al comando** si tocca solo la repo corrente: working tree, index, `.gitshared` in staging, hook (REQ-F-016), oggetti ottenuti con il fetch e una **registrazione locale dell'operazione in attesa** in `.git/config` (tipo di operazione, ID e, per `link`, path della sorgente). Nessun'altra repo viene a sapere nulla.
  - **Al commit** che contiene la modifica a `.gitshared` l'operazione diventa effettiva: ref, sezione locale, membership, annuncio ai peer, elenco dei branch attivi. La completa il `post-commit`; se l'hook non scatta, la completa il primo comando `git shared` successivo. Con `-c` o `-m` il comando committa e completa subito.
  - **Annullamento.** Se prima del commit l'utente riporta `.gitshared` com'era (`git restore`), l'operazione in attesa viene scartata al primo comando `git shared`: non resta traccia nelle altre repo. Gli hook installati restano: senza file shared non fanno nulla.
  - `git shared status` mostra l'operazione come *in attesa di commit*.
  - Restano a effetto immediato i comandi che toccano solo dati locali non versionati: `branch --add/--remove`, `prune`, `relocate`.
- **REQ-F-020 – Rinomina e cancellazione.** `git shared mv <vecchio> <nuovo>` rinomina il file (`git mv`) e aggiorna `path` in `.gitshared`, unico posto in cui è scritto (D-068). La rinomina è locale: i peer non ne sono toccati (il nome nella catena è fisso, D-042). La cancellazione del file in una repo **non si propaga**: `status` la segnala come *incoerente* e propone `git shared unlink` (D-045).
- **REQ-F-022 – Branch attivi.** Per ogni file shared la repo tiene, in locale, l'elenco dei **branch attivi**: voci `branch` nella sezione `[shared "<id>"]` di `.git/config` (D-055). `git shared branch <file>` mostra l'elenco; `--add <branch>` e `--remove <branch>` lo modificano, con effetto immediato: è un dato solo locale e non versionato.
  - **Pattern** (D-056): una voce può essere un nome esatto (`main`) o un pattern con i jolly di git (`release/*`). Il confronto con il branch corrente è delegato a git (`git branch --list <pattern>`), non è implementato dall'estensione.
  - **Default versionato** (D-057): la sezione del file in `.gitshared` può contenere voci `branch`, che sono il default per chi collega la repo. `set` vi scrive il branch corrente. L'elenco locale nasce come **copia** del default al `set`, al `link` e al ri-aggancio dopo un clone (D-043); se il default manca, contiene il solo branch corrente. Dopo la copia vale solo l'elenco locale: una modifica successiva del default non lo cambia.
  - `git shared branch <file> --default --add/--remove <branch>` modifica il default in `.gitshared` e lo mette in staging; non committa (D-007). `git shared branch <file> --reset` ricopia il default nell'elenco locale.
  - Su un branch attivo valgono tutte le regole: controllo H/I/T, pubblicazione, avvisi.
  - Su ogni altro branch, e con HEAD staccata (checkout di un tag, bisect, rebase in corso), il file è **sospeso**: il `pre-commit` non controlla, il commit non pubblica, non ci sono avvisi. Il file si modifica come un file normale.
  - La ricezione non dipende dal branch: `refs/shared/<id>` è per repo (REQ-F-007).
  - `update` e `publish` lanciati a mano su un branch sospeso funzionano, con un avviso: sono atti espliciti dell'utente.
  - Quando le modifiche fatte su un branch sospeso arrivano con un merge su un branch attivo, lo stato si ricava come sempre (D-054): *da pubblicare* o *divergente*. La sospensione rimanda la pubblicazione, non scarta nulla.
- **REQ-F-016 – Installazione nella repo.** `set` e `link` installano nella repo gli hook necessari (`pre-commit`, `post-commit`, `post-checkout`, `post-merge`, `post-rewrite`) **senza mai sovrascrivere hook esistenti**: hook da configurazione con git ≥ 2.54, altrimenti blocco delimitato da marker nei file di hook (D-030). Gli hook sono solo punti d'ingresso e comodità: la correttezza non dipende da loro (D-041).

**Pubblicazione e ricezione**
- **REQ-F-004 – Pubblicazione al commit.** Quando un commit modifica un file shared partendo da una base aggiornata (H = T prima del commit, D-039), il sistema crea una nuova versione in `refs/shared/<id>` con genitore T e la invia a tutti i peer raggiungibili con `git push <peer> refs/shared/<id>` (D-016, D-040). Il push trasferisce anche gli oggetti. *Origine:* D-007, D-013, D-016, D-040.
  - Si pubblica solo dai **branch attivi** del file (REQ-F-022). La pubblicazione automatica si può disattivare con `shared.autoPublish` (default `true`, D-044). Fuori da questi casi la versione resta "da pubblicare" e si pubblica con `git shared publish`.
- **REQ-F-005 – Pubblicazione manuale.** `git shared publish [<path>]` pubblica su richiesta ogni file in stato "da pubblicare" (hook non scattato, merge/pull/rebase, peer prima irraggiungibili, pubblicazione automatica disattivata). *Origine:* D-041.
- **REQ-F-006 – Solo fast-forward.** Una versione si pubblica solo se discende dall'ultima versione nota; il push verso un peer che ha una versione più recente o divergente è rifiutato da git (non fast-forward) e il sistema indica di eseguire `git shared update` (D-009, D-014, D-040).
  - **Regola H/I/T** applicata dal `pre-commit`, con soli controlli locali (D-039). Per ogni file shared non sospeso sul branch corrente (REQ-F-022):
    - I = H → il commit non tocca il file: passa;
    - I = T → si sta applicando un update: passa, nessuna pubblicazione;
    - H = T → modifica su base aggiornata: passa, il `post-commit` pubblica;
    - commit di risoluzione di un merge shared in corso (REQ-F-011): passa;
    - altrimenti → base obsoleta: il commit **passa con un avviso** e non viene pubblicato (D-058). L'avviso dice che il file risulterà *divergente* e che `git shared update` unirà le due versioni.
  - Il `pre-commit` non blocca mai: è l'utente a decidere se aggiornarsi prima o dopo. La protezione non viene dal blocco ma dalla regola fast-forward e dallo stato ricavato (D-041, D-054).
  - Se gli hook non scattano, nulla viene perso: il file risulta "da pubblicare" o "divergente" in `status` (D-041).
- **REQ-F-007 – Ricezione.** Una repo riceve una versione in due modi: (a) dal push di un peer, accettato solo se fast-forward rispetto alla propria `refs/shared/<id>`; (b) con il fetch eseguito da `update` (REQ-F-009). In entrambi i casi arrivano ref **e oggetti**; WT e index non vengono toccati; funziona anche con modifiche non committate (D-015, D-040).
- **REQ-F-008 – Peer irraggiungibili.** Se un peer non è raggiungibile (path inesistente, permessi, lock), la pubblicazione prosegue con gli altri e il peer viene segnalato. *Origine:* I-004.

**Applicazione e conflitti**
- **REQ-F-009 – `update`.** `git shared update [<path>]`: (1) esegue `git fetch` di `refs/shared/<id>` da ogni peer raggiungibile, in ref temporanee (D-040); (2) se una versione ottenuta discende da quella locale, avanza `refs/shared/<id>` in fast-forward; se è divergente, vedi REQ-F-011; (3) applica T nel WT e nell'index a partire dal blob (`update-index --cacheinfo` + `checkout-index`, D-042). Non committa, salvo `-c`/`-m` (D-007, D-015).
- **REQ-F-010 – Modifiche locali.** Se il file shared ha modifiche non committate, `update` si rifiuta di sovrascriverle; `-f` / `--force` sovrascrive (D-022).
- **REQ-F-011 – Divergenza.** Sono divergenti due versioni nessuna delle quali discende dall'altra (D-014): due peer hanno pubblicato senza vedersi, oppure il branch corrente ha modificato il file partendo da una base obsoleta (D-041). `update` esegue un merge a 3 vie del contenuto usando come base il merge-base delle catene (o la base ricavata dal branch); i conflitti sono presentati come in `git merge` (stage 1/2/3 nell'index, marker nel file, §3). Il merge in corso è registrato in uno stato locale in `.git/`; al commit della risoluzione si crea una versione con due genitori, che viene pubblicata.

**Stato e manutenzione**
- **REQ-F-012 – `status`.** `git shared status` elenca per ogni file shared: nome, path, ID e stato. Lo stato è **ricavato** ogni volta da H, I, T e dalla catena, senza dati salvati (D-041), in due passi (D-054): **prima si ricava la base B** (D-041, D-065), **poi** si confronta H con T e con il blob di B. Il solo fatto che H coincida con il blob di una versione della catena non decide lo stato: lo stesso contenuto può comparire in più punti della storia (es. un revert).
  - *allineato*: H = T;
  - *da applicare*: B è più vecchia di T e H = blob di B (il branch è rimasto indietro, senza modifiche proprie);
  - *da pubblicare*: B = T e H ≠ T (modifica su base aggiornata; vale anche se H coincide con il blob di una versione precedente, come dopo un revert);
  - *divergente*: B è più vecchia di T e H ≠ blob di B; oppure nessuna versione della catena compare nella storia del branch (nessuna base, caso D-029); oppure i genitori di un merge danno basi diverse (D-065);
  - *in attesa di commit*: `set`, `link` o `unlink` preparato ma non ancora confermato da un commit (REQ-F-023);
  - *sospeso*: il branch corrente non è tra i branch attivi del file, oppure HEAD è staccata (REQ-F-022); accanto è mostrato comunque lo stato ricavato;
  - *scollegato*: sezione in `.gitshared` senza sezione in `.git/config` (es. dopo un clone): suggerisce `link` (D-048);
  - *incoerente*: sezione in `.git/config` senza sezione in `.gitshared`, file rinominato o cancellato (REQ-F-020).
  - Con `--peers` mostra anche lo stato dei peer (raggiungibile / mancante / versione diversa).
- **REQ-F-013 – Avviso al checkout.** Dopo un checkout, un merge o una riscrittura (hook `post-checkout`, `post-merge`, `post-rewrite`), se un file shared non è *allineato* il sistema avvisa con lo stato e il comando da lanciare, senza modificare nulla (D-010, D-041). Nessun avviso per i file sospesi sul branch di arrivo (REQ-F-022).
- **REQ-F-014 – Peer mancanti.** `git shared prune` rimuove dalla membership i peer che non esistono più, su tutti i peer raggiungibili.
- **REQ-F-015 – Repo spostata.** `git shared relocate <vecchio-path>` comunica ai peer il nuovo path della repo corrente; i peer sostituiscono la voce con il vecchio path (D-023, D-035). `git shared relocate --peer <path>` aggiorna a mano il path di un peer spostato.
- **REQ-F-017 – Rilevamento spostamento.** Ogni repo registra il proprio path (`shared.selfPath`); a ogni comando `git shared`, se la posizione attuale è diversa, il sistema avvisa e suggerisce il comando `relocate` con il vecchio path già compilato. Un peer irraggiungibile durante `publish` o `status` produce un suggerimento analogo (D-035).

**Documentazione utente**
- **REQ-F-018 – Aiuto rapido `-h`.** `git shared -h` stampa la sintassi sintetica di tutti i sottocomandi; `git shared <sottocomando> -h` quella del singolo sottocomando (opzioni, argomenti). Uscita con codice 129, come i comandi git. Testo nella lingua configurata (D-025). *Origine:* D-038.
- **REQ-F-019 – Manuale completo `--help`.** `git shared --help` e `git help shared` mostrano il manuale completo dell'estensione (descrizione, sottocomandi, opzioni, esempi, file e configurazione usati, codici di uscita). *Origine:* D-038.
  - Vincolo tecnico: git intercetta `git <cmd> --help` e lo trasforma in `git help <cmd>`, che cerca la pagina di manuale `git-shared`. `--help` quindi **non arriva mai al nostro eseguibile**. Servono:
    - una **man page** `git-shared.1` installata nel MANPATH (Linux, formato di default `man`);
    - una pagina **HTML** `git-shared.html` nella directory della documentazione di git (Git for Windows usa di default il formato `html`).
  - Ripiego sempre disponibile, anche senza man/HTML installati: `git shared help [<sottocomando>]` stampa il manuale incorporato nell'eseguibile. Lo stesso testo è mostrato se l'eseguibile riceve `--help` direttamente (es. `git-shared --help`).
  - Il manuale è scritto in un unico sorgente da cui si generano man page, HTML e testo incorporato. [APERTO] Formato del sorgente e strumento di generazione (es. AsciiDoc come git): dipendenza solo di build, non di runtime (REQ-NF-002). Lingua del manuale: inglese; italiano per il testo incorporato.

### 5.2 Requisiti non funzionali (REQ-NF-xxx)
- **REQ-NF-001 – Linguaggio.** Il nucleo è in **C11** (sottoinsieme D-031), stile §8. Il C non è una scelta assoluta: parti del sistema possono essere script (`.sh`, `.bat`). La ripartizione tra C e script si decide in architettura (§6, D-063).
- **REQ-NF-002 – Dipendenze.** Nessuna dipendenza di runtime oltre a git e alla libreria C standard / API di sistema (D-017). Gli eventuali script usano solo interpreti già presenti con git o con il sistema operativo (D-063).
- **REQ-NF-003 – Piattaforme.** Linux e Windows (Git for Windows). macOS fuori scope in fase 1 (D-024).
- **REQ-NF-004 – Versione di git.** Versione minima supportata: **git 2.34** (D-032).
- **REQ-NF-005 – Atomicità.** Ogni aggiornamento di `refs/shared/<id>` è atomico e condizionato: `git update-ref <ref> <nuovo> <vecchio>` in locale, regola fast-forward di `git push` verso i peer. Un errore a metà operazione non lascia stato incoerente nella repo locale; un push fallito verso un peer lascia quel peer semplicemente indietro.
- **REQ-NF-006 – Index.** Il sistema non scrive mai direttamente `.git/index` né altri file interni di git: usa solo comandi plumbing (§3).
- **REQ-NF-007 – Prestazioni e memoria.** Prestazioni e occupazione di memoria sono obiettivi primari. Soglie (D-036): `status` solo locale < 100 ms su Linux e < 300 ms su Windows; pubblicazione verso 32 peer < 1 s; memoria del processo (statica + stack + heap, D-066) < 1 MiB.
- **REQ-NF-008 – Interfaccia.** Sintassi e convenzioni come i comandi git (opzioni brevi/lunghe, `-h`, codici di uscita: 0 successo, ≠0 errore). Messaggi in inglese di default, italiano selezionabile (D-025): `git shared lang <en|it>`, che scrive `shared.lang` in `git config --global`.
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
> Embrionale. Si riempie insieme alla prima implementazione minimale.

### 6.0 Perimetro della v0
La v0 è il **core solo locale**: tutto ciò che serve perché due o più repo sulla stessa macchina condividano un file, senza i casi rari. Si costruisce in quattro passi, ognuno provabile da solo.

| Passo | Contenuto | Requisiti |
|---|---|---|
| **M0 – Scheletro** | Moduli di base (§6.2); `git shared -h`, `git shared help`; `status` che elenca le sezioni di `.gitshared` | REQ-F-018, REQ-NF-008 |
| **M1 – Una repo** | `set` (con `-c`/`-m`), creazione della catena, `status` con *allineato*, *da pubblicare*, *in attesa di commit* | REQ-F-001, REQ-F-021, REQ-F-023, REQ-F-012 (parte) |
| **M2 – Due repo** | `link` su destinazione nuova, `publish`, `update` in fast-forward, `status` senza merge | REQ-F-002 (parte), REQ-F-004, REQ-F-005, REQ-F-007, REQ-F-009, REQ-F-010 |
| **M3 – Hook** | Installazione con marker; `pre-commit` che avvisa; `post-commit` che pubblica e completa le operazioni in attesa | REQ-F-006, REQ-F-016, REQ-F-023 |

> **Stato (2026-11-15): M0 e M1 completati.** `make` compila senza warning (`-Wall -Wextra -Wpedantic -Wshadow`), `make test` passa 111 test (§7). Tempi medi su Windows (soglia di REQ-NF-007: 300 ms): `status` con 5 file allineati 81 ms; con un file da pubblicare 93 ms; con un file con 30 commit di storia 118 ms; `set` 157 ms. I file `*_posix.c` non sono ancora stati compilati né provati su Linux.
>
> Limiti noti di M1: la regola dei merge di D-065 (basi diverse dai due genitori → *divergente*) non è applicata; la catena si legge seguendo il primo genitore (le versioni di merge arrivano con M2); sul branch nato prima del `set` lo stato è *incoerente* (A-21).

**Fuori dalla v0**, in ordine di probabile arrivo: merge delle divergenze (REQ-F-011); `link` su destinazione esistente e `--adopt` (D-062, D-064); `unlink`, `mv`; comando `branch` e pattern (nella v0 i branch attivi sono solo nomi esatti, copiati dal default di `.gitshared`); `prune`, `relocate`; seme e ri-aggancio dopo un clone; messaggi in italiano, man page e HTML; hook da configurazione (git ≥ 2.54); prove con SHA-256 (il formato degli oid è già previsto).

### 6.1 Vista di contesto
> Fatti verificati con il prototipo `hello/` (git 2.43, Windows, MinGW-w64 GCC 13.2). Da ripetere su Linux e con git 2.34.

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
| Comandi | `cmd_*` | Un file per sottocomando: `set`, `link`, `status`, `update`, `publish`, `hook`, `help` |
| Modello | `shared` | La repo (`Repo_t`: radice, branch, voci) e i file shared (`.gitshared` + sezioni locali); completamento delle operazioni in attesa (REQ-F-023) |
| | `state` | Derivazione dello stato e della base (D-054, D-065) |
| | `chain` | Catena `refs/shared/<id>`: creazione di una versione, blob delle versioni |
| | `peers` | Membership; push e fetch verso i peer (M2) |
| | `hooks` | Installazione dei blocchi con marker (M3) |
| Accesso a git | `git` | Lancia comandi git; oid in binario (`Oid_t`, 33 byte); dialogo con `cat-file --batch-check` (`GitBatch_t`) |
| Piattaforma | `proc` | Processi **senza shell**, con stdin e stdout in pipe: `proc_win.c` (`CreateProcess`), `proc_posix.c` (`fork` + `exec`) |
| | `sys` | Argomenti in UTF-8, numeri casuali (`BCryptGenRandom`, `/dev/urandom`), file con path UTF-8 |
| | `path` | Normalizzazione dei path (A-26) |
| Base | `str`, `mem` | Stringhe a lunghezza variabile, `xmalloc` (D-066) |
| | `bounds`, `err` | Limiti (D-034); codici di errore interni e codici di uscita |
| | `msg` | Tabelle dei messaggi (§8.5) |

#### Alberatura
```
git-shared-file/
├── project.md, breef.md, reportIdea.md
├── Makefile                  build Linux e Windows (MinGW)
├── src/                      il core
│   ├── main.c
│   ├── cmd.h                 prototipi dei sottocomandi
│   ├── cmd_set.c  cmd_link.c  cmd_status.c  cmd_update.c
│   ├── cmd_publish.c  cmd_hook.c  cmd_help.c
│   ├── shared.c/.h  state.c/.h  chain.c/.h  peers.c/.h  hooks.c/.h
│   ├── git.c/.h
│   ├── path.c/.h
│   ├── bounds.h
│   └── msg.c/.h
├── utils/                    moduli di base, senza dipendenze da src/
│   ├── proc.c/.h  proc_win.c  proc_posix.c
│   ├── sys.h  sys_win.c/.h  sys_posix.c
│   ├── str.c/.h  mem.c/.h
│   └── err.h
├── doc/                      sorgente del manuale (A-06)
├── test/
│   ├── lib.sh                funzioni comuni: repo di prova, asserzioni
│   └── t0001-<scenario>.sh   uno scenario per file, numerati come i test di git
├── hello/                    prototipo, da eliminare quando src/ lo sostituisce
└── build/                    generato
```

### 6.3 Formati dati e file del protocollo
Tutti i file in formato config di git si leggono e scrivono **solo** con `git config` (`-f <file>` per quelli fuori da `.git/config`): nessun parser nostro.

| Dato | Dove | Versionato | v0 |
|---|---|---|---|
| Dichiarazione dei file shared | `.gitshared` alla radice | sì | sì |
| Configurazione locale della repo | `.git/config`, sezione `[shared]` | no | sì |
| Dati locali di un file shared | `.git/config`, sezione `[shared "<id>"]` | no | sì |
| Lingua | `~/.gitconfig`, `shared.lang` | no | no |
| Catena delle versioni | `refs/shared/<id>` | no (replicata tra peer) | sì |
| Versioni ottenute da `update` | `refs/shared-incoming/<id>/<k>` | no, temporanee | sì |
| Hook | blocco con marker in `hooks/<evento>` | no | sì |
| Merge in corso | `.git/shared/merge-<id>` | no | no |
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
	autoPublish = true
[shared "0f8c3e5a9b2d4c71a6e0d9b8c7f61234"]
	branch = main
	peer = C:/Users/luca/progetti/B
	peer = C:/Users/luca/progetti/C
	pending = link
	pendingSource = C:/Users/luca/progetti/B
```
- `shared.selfPath`: radice della repo stessa, normalizzata (D-035, A-26).
- `shared.autoPublish`: default `true` (D-044).
- `shared.<id>.branch`: branch attivi, nomi esatti nella v0 (D-055).
- `shared.<id>.peer`: radice di ogni peer, normalizzata; al massimo 32 (D-034).
- `shared.<id>.pending` (`set`, `link`, `unlink`) e `pendingSource` (solo `link`): operazione in attesa di commit (REQ-F-023). Spariscono quando l'operazione si completa o viene scartata.
- **Niente `name` né `path`** (D-068): stanno in `.gitshared`, che è per branch; duplicarli in config crea incoerenze (A-21).

#### Catena `refs/shared/<id>`
- Ogni versione è un commit il cui tree ha **una sola voce**: `100644 blob <oid>	content` (D-042). Modo sempre `100644` nella v0 (bit di esecuzione: A-15).
- Genitori: nessuno per la prima versione, uno per una versione normale, due per un merge.
- Autore e data: quelli dell'utente, come per ogni commit (D-013). Messaggio: `shared: <nome>`. Commit deterministici: A-14.
- Creazione con il solo plumbing: `git mktree` (riga `100644 blob <oid>\tcontent`), `git commit-tree <tree> [-p <T>] -m <msg>`, `git update-ref refs/shared/<id> <nuovo> <T>`. L'ultimo argomento rende l'aggiornamento condizionato (REQ-NF-005); per la prima versione si passa il valore vuoto, che richiede che la ref non esista.
- Lettura: blob di T = `git rev-parse refs/shared/<id>:content`; ordine = `git merge-base --is-ancestor`.
- Pubblicazione: `git push <peer> refs/shared/<id>:refs/shared/<id>` (D-040), senza `+`: il fast-forward lo verifica git.

#### Ref temporanee di `update`
`git fetch <peer> +refs/shared/<id>:refs/shared-incoming/<id>/<k>`, con `k` = posizione del peer nell'elenco. Qui il `+` è corretto: sono copie di lavoro. Si cancellano a fine `update` (`git update-ref -d`). Il prefisso `refs/shared-incoming/` non ricade sotto `refs/shared/`, quindi non si confonde con le catene.

#### Hook (v0: blocco con marker)
- Cartella degli hook da `git rev-parse --git-path hooks`, che rispetta `core.hooksPath`.
- Blocco, uguale per ogni evento:
  ```sh
  # >>> git shared >>>
  git shared hook pre-commit "$@"
  # <<< git shared <<<
  ```
- Hook assente: si crea il file con `#!/bin/sh` e il blocco, con bit di esecuzione.
- Hook presente: il blocco si inserisce **subito dopo la prima riga**, non in fondo: un hook che termina con `exit` non lo raggiungerebbe. Mai sovrascrivere (D-030).
- Il blocco non contiene `exit`: non cambia l'esito dell'hook che lo ospita, e il `pre-commit` non blocca mai (D-058).

### 6.4 Flussi principali
Regola per tutti i flussi: **pochi processi git**, perché su Windows ognuno costa ~30 ms (§6.1). Processi indipendenti si avviano insieme; molte domande sugli oggetti passano da un solo `cat-file --batch-check`.

#### `status`
1. In parallelo: `rev-parse --show-toplevel --show-prefix`, `symbolic-ref -q HEAD` (branch corrente: funziona anche su un branch senza commit, `rev-parse HEAD` no), `config -z --show-scope --get-regexp ^shared\.` (sezioni locali).
2. `config -z -f <top>/.gitshared --get-regexp ^shared\.`, mentre gli altri due finiscono.
3. Un `cat-file --batch-check` per tutte le domande: `HEAD:<path>` (H) e `refs/shared/<id>:content` (T) di ogni file.
4. Solo se c'è un'operazione in attesa: `config --blob HEAD:.gitshared` e il completamento.
5. Solo se H ≠ T: blob delle versioni sullo stesso `cat-file` (due domande per versione) e un `git log --raw` sul path del file (D-065), interrotto appena possibile.

Caso comune (tutti i file allineati): **5 processi, qualunque sia il numero di file**.

#### `set`
1. Come `status`, passi 1–4; poi `ls-files -s --full-name` con pathspec letterali: risolve il path, controlla che sia tracciato, senza conflitti e un file regolare.
2. ID da 16 byte casuali (UUID v4).
3. Attesa in `.git/config` **per prima**: se un passo successivo fallisce, il primo comando la scarta da solo (REQ-F-023).
4. Sezione in `.gitshared` (`git config -f`: id, path, branch); in caso di errore `.gitshared` torna com'era.
5. `update-index --add -- .gitshared`.
6. Con `-c`/`-m`: `commit --only -- .gitshared`, poi il completamento.

#### Completamento di un `set` (REQ-F-023)
Quando la sezione con quell'ID compare in `HEAD:.gitshared`: (1) branch attivi copiati dal default di `.gitshared`, o il branch corrente; (2) prima versione della catena (`mktree` dallo stdin, `commit-tree`, `update-ref` con valore vecchio vuoto: solo se la ref non esiste); (3) rimozione dell'attesa. L'ordine permette di ripetere tutto dopo un'interruzione. Se il file non è ancora in HEAD, l'attesa resta e l'utente riceve un avviso. Se la sezione non è né in HEAD né nel working tree, l'attesa viene scartata.
### 6.5 Gestione errori

## 7. Strategia di verifica
<!-- Matrice di tracciabilità V-cycle: REQ ↔ test di sistema / integrazione / unità. -->
> Embrionale: per ora solo test di sistema, che lanciano `git shared` come farebbe l'utente.

- **Struttura.** `test/t<NNNN>-<scenario>.sh`, uno scenario per file; `test/lib.sh` con le funzioni comuni; `test/run.sh` li esegue tutti (`make test`).
- **Isolamento.** Ogni file lavora in una cartella temporanea cancellata alla fine, con `build/` in testa al `PATH`, `HOME` temporanea e `GIT_CONFIG_NOSYSTEM=1`: la configurazione git dell'utente non influisce.
- **Formato.** Output TAP (`ok N - ...`, `not ok N - ...`, piano `1..N`); codice di uscita 1 se un test fallisce.

| Test | Requisiti | Test |
|---|---|---|
| `t0001-usage.sh` | REQ-F-018, REQ-F-019, REQ-NF-008, D-067 | 19 |
| `t0002-status.sh` | REQ-F-012 (parte), REQ-F-021, REQ-NF-003 e D-067 (path con spazi e non ASCII, senza shell), §6.3 | 25 |
| `t0003-set.sh` | REQ-F-001, REQ-F-021, REQ-F-022 (sospensione), REQ-F-023 (attesa, completamento, annullamento), D-006, D-007, D-057 | 59 |
| `t0004-base.sh` | REQ-F-012, D-054, D-065 (revert, `reset`, B-14), con catene di più versioni costruite a mano | 8 |

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
- Messaggi utente in tabelle statiche di stringhe (`const char *const msg_en[MSG_COUNT]`, `msg_it[...]`) indicizzate da un `enum`: nessuna allocazione, nessuna dipendenza (niente gettext).

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
| D-021 | Controllo in due fasi: hook **`pre-commit`** applica la regola H/I/T (D-039) con soli controlli locali e **avvisa** sui commit da base obsoleta (rev. 1, D-058: prima li bloccava); hook **`post-commit`** pubblica. Se gli hook sono saltati o non scattano (`--no-verify`, merge, pull, rebase, cherry-pick), lo stato resta ricavabile (D-041) e si recupera con `status` / `publish` / `update`. | Segnala prima di creare il commit senza far dipendere la correttezza dagli hook. | REQ-F-006, I-015 |
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
| D-032 | Versione minima di git: **2.34** (Ubuntu 22.04 LTS). Matrice di test: 2.34 + ultima stabile, Linux e Windows. | Tutti i plumbing previsti sono disponibili; versione LTS diffusa. | I-011 |
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
| D-044 | Pubblicazione automatica **configurabile**: `shared.autoPublish` (default `true`). (Rev. 1: `shared.publishBranch`, con default "tutti i branch", è sostituito dall'elenco dei branch attivi, D-055.) | Un commit su un branch sperimentale non deve per forza raggiungere tutti i peer. | I-015 |
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
  - `pre-commit` controlla e avvisa, senza mai bloccare (D-058); `post-commit` pubblica (D-021). Il controllo è solo locale: i peer li verifica il push (D-039).
  - Gli hook non coprono merge, pull, rebase, cherry-pick e `--no-verify`. Per questo lo **stato è sempre ricavabile** e gli hook sono solo comodità: `status`, `publish` e `update` recuperano (D-041).
  - Al cambio di branch si avvisa, non si modifica nulla (D-010).
  - Il file è shared solo sui **branch attivi**, scelti dall'utente e salvati in locale; altrove è sospeso e si modifica liberamente (D-055). La pubblicazione automatica si può anche disattivare del tutto (D-044).
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
| Versione di git | Minimo 2.34; hook da configurazione dove c'è 2.54 | D-032, D-030 |
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
| A-06 | **Formato del manuale.** Sorgente unico da cui generare man page, HTML e testo incorporato (es. AsciiDoc). | Aperto in REQ-F-019 |
| A-07 | **Variante puntatore** (clean/smudge), utile solo se i contenuti stanno fuori dalla repo (file grandi). | Evoluzione futura |
| A-08 | **Propagazione ad anello** al posto del broadcast. | Evoluzione futura |
| A-09 | **Directory, file binari, macOS.** | Evoluzioni future |
| A-17 | **Licenza.** | Ultima cosa da decidere, a progetto terminato e funzionante |
| A-18 | **Ripartizione tra C e script** (`.sh`, `.bat`): cosa fa il programma C e cosa gli script (D-063). | Da decidere in architettura (§6) |
| A-12 | **Momento della pubblicazione in locale.** Al commit è presto: i commit locali si riscrivono (amend, reset, squash) e la versione è già nei peer (reportIdea B-05). | Da valutare |
| A-13 | **Riconciliazione della membership.** Se un peer non è raggiungibile durante `link`/`unlink`, gli elenchi restano diversi; `prune` non distingue "cancellato" da "non raggiungibile ora" (reportIdea B-07). Un peer che non riceve la notifica di `unlink` continua a inviare la ref, che ricompare orfana senza che nessuno stato la descriva (reportIdea B-27). Collegata ad A-02. | Da valutare |
| A-14 | **Commit della catena deterministici**, perché la stessa modifica fatta in due repo non risulti divergente (reportIdea B-09). | Da valutare |
| A-15 | **Fine riga e bit di esecuzione** uguali tra i peer: controllo al `link` o contenuto sempre normalizzato nella catena (reportIdea B-12). | Da valutare |
| A-16 | **`git shared update --abort`** e controllo di validità del merge in corso (reportIdea B-13). | Da valutare |
| A-20 | **Stessa regola per `pre-commit` e `status`** (reportIdea B-15). L'hook usa H = T, lo stato B = T (base di D-065): con `autoPublish=false` o dopo un hook saltato ogni commit dà un falso avviso di base obsoleta. Un'unica funzione di derivazione. | Da valutare |
| A-21 | **`.gitshared` per branch, membership per repo** (reportIdea B-16). Sui branch nati prima del `set`, o dopo un `unlink`, lo stato è *incoerente* o *scollegato* e il comando suggerito (`unlink`, `link`) è dannoso. Proposta: sezione assente sul branch corrente = file non presente qui, trattato come sospeso. | Da valutare |
| A-22 | **T che avanza durante un merge**, o tra `pre-commit` e `post-commit` (reportIdea B-17). Lo stato del merge registra T all'inizio; se al commit è cambiata, niente pubblicazione e nuovo `update`. Il `post-commit` confronta il blob di `HEAD^` con la T attuale. | Da valutare |
| A-23 | **Due divergenze nello stesso `update`** (reportIdea B-18): catena divergente tra peer e branch divergente insieme. Proposta: prima la catena, poi il branch; al più un merge per `update`. Collegata ad A-16. | Da valutare |
| A-24 | **Collisione di nomi in `.gitshared`** (reportIdea B-20). Sezione per nome, nome di default = nome del file: `a/config.h` e `b/config.h` finiscono nella stessa sezione. Chiave per ID o controllo di unicità. | Da valutare |
| A-25 | **Elenco dei branch attivi senza corrispondenze** (reportIdea B-21). Dopo `git branch -m` il file diventa sospeso senza avvisi. Proposta: `status` e hook (anche `reference-transaction`, git ≥ 2.28) segnalano le voci che non corrispondono a nessun branch esistente. | Rimedio proposto, da confermare |
| A-26 | **Spostamento o copia** (reportIdea B-22). `relocate` dopo una copia esclude dai peer la repo originale; path Windows scritti in forme diverse danno falsi spostamenti e peer doppi. `relocate` verifica che il vecchio path non esista più; i path si normalizzano prima di salvarli e confrontarli. | Da valutare |
| A-27 | **Ambiente degli hook** (reportIdea B-23). Nel `pre-commit` l'index è quello di `GIT_INDEX_FILE` (verificato: `index.lock` con `commit -a`, `next-index-*.lock` con `commit -- <path>`). Le operazioni sui peer vanno lanciate senza `GIT_DIR`, `GIT_WORK_TREE`, `GIT_INDEX_FILE`. Da portare nei requisiti (REQ-NF-006). | Da valutare |
| A-28 | **Fast-forward garantito solo dal client** (reportIdea B-24). `--force`, `--mirror` (verificato: cancella `refs/shared/*` sulla destinazione) e `--prune` lo scavalcano. Il rifiuto può essere "fetch first" oltre a "non-fast-forward": `publish` deve riconoscerli entrambi. Collegata ad A-01. | Da valutare per la fase 2 |
| A-29 | **Commit dedicato non garantito** (reportIdea B-25). `-c`/`-m` devono committare solo il file shared e `.gitshared` (`git commit --only`); il commit manuale dopo `update` può contenere altro. | Da valutare |
| A-30 | **Stato "applicato, da committare"** (reportIdea B-26). Dopo `update` senza commit, `status` dice ancora *da applicare* e un secondo `update` rifiuta per "modifiche locali" che sono la versione precedente. | Da valutare |