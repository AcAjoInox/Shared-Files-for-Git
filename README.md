# Git Shared Files

## A cosa serve
Capita spesso di usare lo stesso file in più progetti git: un header comune, un file di configurazione, uno script. Oggi lo si copia a mano, e prima o poi le copie finiscono per essere diverse.

`git shared` è un'estensione di git che tiene **lo stesso file allineato in più repository**. Modifichi il file in un progetto qualsiasi, e gli altri progetti vengono a sapere che esiste una versione nuova. Ogni progetto la adotta quando vuole, con un comando.

Il file resta un file normale: chi apre la repo senza l'estensione lo vede, lo compila e lo usa come sempre.

Si può usare in due modi:
- **in locale**, tra repo sulla stessa macchina. È la prima versione dell'estensione;
- **in remoto**, tra repo ospitate su GitHub, GitLab (in fase di sviluppo).

## Informativa

Il codice è stato implementato interamente da un modello di AI sotto la supervisione umana. Idee, test e dubug sono guidati da un umano.

## Installazione
Serve: git, `make` e `gcc` (su Windows MinGW-w64). Dalla cartella del progetto:

```bash
make            # compila
make install    # copia git-shared in ~/bin (Windows: %USERPROFILE%\bin)
git shared -h   # verifica
```

Se `git shared` non viene trovato, aggiungi quella cartella al `PATH`. Su Windows, per avere anche `git shared --help`, lancia `make install` da amministratore.

## I concetti di base

**File shared.** Un file che hai dichiarato condiviso. Ogni repo che lo usa ne ha una copia vera, tracciata come ogni altro file. Il path può essere diverso da una repo all'altra.

**`.gitshared`.** Un file nella cartella principale della repo che elenca i file shared. Lo gestisce l'estensione e viene versionato con la repo, come `.gitmodules` per i submodule. Se una repo ha questo file, contiene file shared.

**Peer.** Le altre repo che usano lo stesso file. Non esiste una repo "principale": chi ha dichiarato il file per primo non ha privilegi, tutte possono modificarlo.

**Versioni shared.** Ogni file shared ha una sua piccola storia di versioni, separata dalla storia del progetto e uguale in tutti i peer. Serve a stabilire quale versione è più recente di un'altra.

**Nessun servizio centrale.** Non c'è un server né una cartella globale. Tutto sta dentro le repo, che si parlano direttamente.

## Le tre azioni
| Azione | Cosa fa |
|---|---|
| **Pubblicare** | Crea una nuova versione del file e la annuncia ai peer raggiungibili |
| **Ricevere** | Il peer scarica la versione nuova nella propria storia delle versioni del file. I suoi file **non vengono toccati** |
| **Applicare** | `git shared update` porta la versione nel tuo progetto; poi confermi con un commit |

Ricevere e applicare sono separati di proposito: nessuno ti cambia un file sotto le mani mentre lavori.

## Le regole, valide ovunque
1. **Conta solo ciò che è committato.** Una modifica non committata non viene mai pubblicata. Finché non fai commit puoi annullare tutto con `git restore`. Vale anche per `set`, `link` e `unlink`: le altre repo ne vengono a sapere solo al commit.
2. **Decidi sempre tu.** I comandi `git shared` preparano le modifiche ma non fanno commit da soli. Se vuoi il commit immediato usa `-c` oppure `-m "messaggio"`: il commit contiene solo il file shared e `.gitshared`, e il resto che hai in staging resta lì.
3. **Meglio aggiornarsi prima di modificare, ma nessuno ti blocca.** Se la tua copia è indietro rispetto all'ultima versione e committi una modifica al file shared, il commit passa, ma la modifica non viene pubblicata: il file risulta *divergente* (lo dicono `status` e `publish`) e `git shared update` unisce la tua versione con l'ultima.
4. **Il tuo lavoro non si perde.** `update` si rifiuta di sovrascrivere modifiche non committate, anche quelle in staging. Per forzare serve `-f`. Una versione portata da un `update` precedente e non ancora committata non conta come modifica tua: un nuovo `update` la sostituisce con l'ultima.
5. **Nessuna modifica viene scartata.** Se due repo hanno modificato il file ognuna per conto suo, le versioni risultano *divergenti* e `git shared update` le unisce come farebbe `git merge`. Se toccano le stesse righe trovi i soliti marker di conflitto: li risolvi, fai `git add` e rilanci `git shared update`, poi commit e `git shared publish`. Per rinunciare basta `git shared update --abort`: il file torna quello dell'ultimo commit.

## Uso in locale
Le repo sono sulla stessa macchina e si conoscono tramite il loro path.

**Quando si pubblica:** solo quando lo decidi tu, con `git shared publish`. Puoi committare, correggere, rifare il commit: i peer ricevono solo la versione che pubblichi. Il commit non costa nulla in più: git shared non interviene.

**Come arriva l'ultima versione:** il comando `publish` lascia un piccolo avviso nella cartella `.git` delle altre repo, senza toccare altro. Ogni repo scarica la versione da sé al suo primo comando `git shared` (di solito `status` o `update`). Se una repo non era raggiungibile (spostata, disco scollegato), rilancia `publish`, oppure lancia `git shared update --all` da quella repo. Se due repo pubblicano insieme arrivano entrambe le versioni, e `update` le unisce.

**Esempio Base:**

```bash
# Nel progetto A: dichiaro shared l'header
cd ~/progetti/A
git shared set include/common.h
git commit -m "common.h diventa shared"

# Nel progetto B: lo collego
cd ~/progetti/B
git shared link ~/progetti/A/include/common.h inc/common.h
git commit -m "collego common.h"      # da qui A sa che B usa il file

# Più tardi, in B: modifico l'header
git commit -am "aggiungo MAX_LEN in common.h"
git shared publish                    # la nuova versione arriva ad A

# Torno in A
cd ~/progetti/A
git shared status                     # common.h: versione nuova da applicare
git shared update
git commit -m "aggiorno common.h"
```

**Se nel progetto c'è già una copia del file**, `link` non la sovrascrive: riusa quello che c'è. Se la copia è uguale a una versione del file shared, entra come *allineata* o *da applicare*, senza conflitti. Se è stata modificata partendo da una versione nota, entra come *divergente* e `git shared update` la unisce. Solo se le due copie non hanno niente in comune devi scegliere tu: `-f` per sovrascrivere, `--merge` per unirle a mano. *Non ancora nella prima versione:* per ora `link` rifiuta una destinazione che esiste già, salvo quando è lo stesso file shared rimasto scollegato (per esempio dopo un clone): allora lo ricollega senza toccarlo.

**Se cambi branch** e lì il file shared è indietro, ricevi un avviso. Non viene modificato niente in automatico.

**Decidi tu su quali branch il file è shared.** All'inizio lo è solo sul branch in cui hai lanciato `set` o `link`. Sugli altri branch è *sospeso*: lo modifichi come un file normale, senza blocchi, avvisi né pubblicazioni. Con `git shared branch` aggiungi o togli un branch dall'elenco. In futuro si potrà indicare anche una famiglia di branch, per esempio `release/*`; la prima versione accetta solo nomi esatti. L'elenco è locale alla repo. In `.gitshared` c'è un elenco di default, che viene copiato quando colleghi la repo (per esempio dopo un clone). Quando poi fai merge su un branch dove il file è shared, le tue modifiche risultano da pubblicare: non si perde niente.

**Se cancelli una repo**, le altre continuano a funzionare. `git shared prune` la toglie dall'elenco dei peer, in tutte le repo; una repo che in quel momento non si riesce a leggere resta. `git shared prune -n` mostra cosa toglierebbe.

**Se sposti una repo**, al primo comando `git shared` l'estensione se ne accorge e ti suggerisce `git shared relocate`, che avvisa gli altri progetti. **Se la copi**, la copia lo dice allo stesso modo: `git shared relocate --copy` la fa diventare un progetto in più, e l'originale resta.

**Se rinomini il file**, usa `git shared mv`. La rinomina vale solo per quella repo. *Non ancora nella prima versione:* per ora un file shared rinominato o cancellato risulta *incoerente*.

## Uso in remoto (seconda versione)
In via di svilippo (prossimamente qui).

## I comandi
| Comando | Cosa fa |
|---|---|
| `git shared set <file>` | Dichiara shared un file della repo corrente |
| `git shared link <file-in-altra-repo> [<destinazione>]` | Collega alla repo corrente un file shared di un'altra repo |
| `git shared unlink <file>` | Toglie il file dalla condivisione; resta come file normale. Al commit gli altri progetti dimenticano questo |
| `git shared status [--peers]` | Mostra lo stato di ogni file shared; con `--peers` anche se gli altri progetti hanno la stessa versione |
| `git shared update [--all] [<file>]` | Prende le versioni annunciate dai peer (con `--all` da tutti i peer) e applica l'ultima; con `--abort` rinuncia a un'unione in corso |
| `git shared publish [<file>]` | Crea la nuova versione e la annuncia ai peer |
| `git shared hook install` | Installa gli hook di avviso, per esempio in una repo i cui file shared non li hanno ancora |
| `git shared mv <vecchio> <nuovo>` | Rinomina un file shared (non ancora nella prima versione) |
| `git shared branch [<file>] [-a\|-rm <branch>]` | Mostra o modifica i branch su cui il file è shared (effetto immediato; `-a` e `-rm` sono le forme brevi di `--add` e `--remove`); con `--default` modifica l'elenco di default in `.gitshared`, con `--reset` lo ricopia in locale. Solo nomi esatti |
| `git shared prune [-n]` | Toglie dall'elenco i peer cancellati o che non condividono più il file |
| `git shared relocate [--copy]` | Avvisa i peer che questa repo è stata spostata (o, con `--copy`, copiata); `--peer <vecchio> <nuovo>` corregge il path di un peer spostato |
| `git shared lang [en\|it]` | Mostra o sceglie la lingua del manuale; per ora i messaggi restano in inglese |
| `git shared <comando> --help` | La pagina di aiuto del comando; `git shared help` elenca i comandi, `git shared <comando> -h` riassume le opzioni |

Gli stati che vedi in `git shared status`:

| Stato | Significato | Cosa fare |
|---|---|---|
| allineato | Hai l'ultima versione | Niente |
| da applicare | Esiste una versione più recente | `git shared update` |
| da committare | Hai applicato l'ultima versione con `update`, ma non hai ancora fatto commit | `git commit`, oppure `git restore --staged --worktree <file>` per annullare |
| da pubblicare | Hai una modifica che i peer non hanno | `git shared publish` |
| divergente | Tu e un peer avete modificato il file separatamente | `git shared update`, che unisce |
| unione in corso | `update` ha trovato un conflitto, oppure l'hai risolto e messo in staging | Correggi il file, `git add`, poi `git shared update`; per rinunciare `git shared update --abort` |
| in attesa di commit | Hai lanciato `set`, `link` o `unlink` ma non hai ancora committato | `git commit`, oppure `git restore` per annullare |
| incoerente | Il file è stato rinominato o cancellato fuori da `git shared`, oppure `.gitshared` è stato modificato a mano | `git shared unlink` (`git shared mv` non è ancora disponibile), oppure `git restore .gitshared` |
| sospeso | Su questo branch il file non è gestito come shared. Vale anche sui branch nati prima del `set`, dove il file non è dichiarato: `status` dice su quale branch lo è. Se nessuno dei suoi branch esiste più (es. lo hai rinominato) te lo dicono `status` e gli avvisi | Niente, oppure `git shared branch <file> -a <branch>` |
| scollegato | La repo dichiara il file ma non conosce i peer (clone) | `git shared link` |

## Aiuto e lingua
L'aiuto ha tre livelli, dal più breve al più completo:

| Comando | Cosa mostra |
|---|---|
| `git shared -h` | I comandi in una riga ciascuno, e come aprire il manuale |
| `git shared <comando> -h` | Le opzioni di un comando, in breve |
| `git shared help` (o `git shared --help`) | La pagina generale del manuale: cos'è, come iniziare, l'elenco dei comandi |
| `git shared <comando> --help` (o `git shared help <comando>`) | La pagina completa di un comando, con esempi. Per esempio `git shared link --help` |

**`--help` e `help` mostrano le stesse pagine.** L'unica differenza è `git shared --help` senza comando: lo intercetta git, che apre la pagina installata da `make install` (nel browser su Windows, con `man` su Linux). `git shared help` invece funziona sempre, anche senza installazione.

**Il manuale è in inglese o in italiano:**
```
git shared lang it      # manuale in italiano, per tutti i tuoi repository
git shared link --help  # ora in italiano
git shared lang en      # di nuovo in inglese
git shared lang         # mostra la lingua attuale
```
La lingua si salva nella configurazione globale di git (`shared.lang`). Per ora vale per il manuale; i messaggi dei comandi restano in inglese. Per `git shared --help` conta la lingua scelta quando hai lanciato `make install`: dopo averla cambiata, rilancia `make install`.

Le pagine sono file di testo nella cartella `help/` del progetto (`help/it/` per l'italiano), una per comando: si possono leggere anche lì, come una wiki.

## Limiti della prima versione
- Solo uso **in locale**.
- Solo **file di testo** singoli: niente cartelle, niente file binari.
- Linux e Windows. Serve git 2.34 o più recente.
- Al massimo 32 peer per file shared; path fino a 4096 caratteri. Nessun limite al numero di file shared per repo.
- L'estensione va installata su ogni macchina che deve pubblicare o applicare versioni. Chi non ce l'ha usa comunque la repo normalmente.
- Non ci sono ancora: `git shared mv`, `link` su una copia del file che esiste già,  messaggi dei comandi in italiano (il manuale in italiano c'è).
