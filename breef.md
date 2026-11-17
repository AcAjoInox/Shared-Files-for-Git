# git shared — in breve

## A cosa serve
Capita spesso di usare lo stesso file in più progetti git: un header comune, un file di configurazione, uno script. Oggi lo si copia a mano, e prima o poi le copie finiscono per essere diverse.

`git shared` è un'estensione di git che tiene **lo stesso file allineato in più repository**. Modifichi il file in un progetto qualsiasi, e gli altri progetti vengono a sapere che esiste una versione nuova. Ogni progetto la adotta quando vuole, con un comando.

Il file resta un file normale: chi apre la repo senza l'estensione lo vede, lo compila e lo usa come sempre.

Si può usare in due modi:
- **in locale**, tra repo sulla stessa macchina. È la prima versione dell'estensione;
- **in remoto**, tra repo ospitate su GitHub, GitLab o simili. È previsto per la seconda versione.

Il meccanismo è lo stesso. Cambia solo come le repo si raggiungono e il momento in cui le versioni vengono inviate.

## I concetti di base

**File shared.** Un file che hai dichiarato condiviso. Ogni repo che lo usa ne ha una copia vera, tracciata come ogni altro file. Il path può essere diverso da una repo all'altra.

**`.gitshared`.** Un file nella cartella principale della repo che elenca i file shared. Lo gestisce l'estensione e viaggia con la repo, come `.gitmodules` per i submodule. Se una repo ha questo file, contiene file shared.

**Peer.** Le altre repo che usano lo stesso file. Non esiste una repo "principale": chi ha dichiarato il file per primo non ha privilegi, tutte possono modificarlo.

**Versioni shared.** Ogni file shared ha una sua piccola storia di versioni, separata dalla storia del progetto e uguale in tutti i peer. Serve a stabilire quale versione è più recente di un'altra.

**Nessun servizio centrale.** Non c'è un server né una cartella globale. Tutto sta dentro le repo, che si parlano direttamente, come i remote di git.

## Le tre azioni
| Azione | Cosa fa |
|---|---|
| **Pubblicare** | Crea una nuova versione del file e la invia ai peer raggiungibili |
| **Ricevere** | Il peer registra che esiste una versione nuova. I suoi file **non vengono toccati** |
| **Applicare** | `git shared update` porta la versione nel tuo progetto; poi confermi con un commit |

Ricevere e applicare sono separati di proposito: nessuno ti cambia un file sotto le mani mentre lavori.

## Le regole, valide ovunque
1. **Conta solo ciò che è committato.** Una modifica non committata non viene mai pubblicata. Finché non fai commit puoi annullare tutto con `git restore`. Vale anche per `set`, `link` e `unlink`: le altre repo ne vengono a sapere solo al commit.
2. **Decidi sempre tu.** I comandi `git shared` preparano le modifiche ma non fanno commit da soli. Se vuoi il commit immediato usa `-c` oppure `-m "messaggio"`: il commit contiene solo il file shared e `.gitshared`, e il resto che hai in staging resta lì.
3. **Meglio aggiornarsi prima di modificare, ma nessuno ti blocca.** Se la tua copia è indietro rispetto all'ultima versione e committi una modifica al file shared, il commit passa, ma la modifica non viene pubblicata: il file risulta *divergente* (lo dicono `status` e `publish`) e `git shared update` unisce la tua versione con l'ultima.
4. **Il tuo lavoro non si perde.** `update` si rifiuta di sovrascrivere modifiche non committate, anche quelle in staging. Per forzare serve `-f`. Una versione portata da un `update` precedente e non ancora committata non conta come modifica tua: un nuovo `update` la sostituisce con l'ultima.
5. **Nessuna modifica viene scartata.** Se due repo hanno modificato il file ognuna per conto suo, le versioni risultano *divergenti* e `git shared update` le unisce come farebbe `git merge`. Se toccano le stesse righe trovi i soliti marker di conflitto: li risolvi, fai `git add` e rilanci `git shared update`, poi commit e `git shared publish`. Per rinunciare basta `git restore -s HEAD -SW <file>`.

## Uso in locale
Le repo stanno sulla stessa macchina e si conoscono tramite il loro path.

**Quando si pubblica:** solo quando lo decidi tu, con `git shared publish`. Puoi committare, correggere, rifare il commit: i peer ricevono solo la versione che pubblichi. Il commit non costa nulla in più: git shared non interviene.

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
vim inc/common.h
git commit -am "aggiungo MAX_LEN"
git shared publish                    # la nuova versione arriva ad A

# Torno in A
cd ~/progetti/A
git shared status                     # common.h: versione nuova da applicare
git shared update
git commit -m "aggiorno common.h"
```

**Se nel progetto c'è già una copia del file**, `link` non la sovrascrive: riusa quello che c'è. Se la copia è uguale a una versione del file shared, entra come *allineata* o *da applicare*, senza conflitti. Se è stata modificata partendo da una versione nota, entra come *divergente* e `git shared update` la unisce. Solo se le due copie non hanno niente in comune devi scegliere tu: `-f` per sovrascrivere, `--merge` per unirle a mano.

**Se cambi branch** e lì il file shared è indietro, ricevi un avviso. Non viene modificato niente in automatico.

**Decidi tu su quali branch il file è shared.** All'inizio lo è solo sul branch in cui hai lanciato `set` o `link`. Sugli altri branch è *sospeso*: lo modifichi come un file normale, senza blocchi, avvisi né pubblicazioni. Con `git shared branch` aggiungi o togli un branch dall'elenco; puoi indicare anche una famiglia di branch, per esempio `release/*`. L'elenco è locale alla repo. In `.gitshared` c'è un elenco di default, che viene copiato quando colleghi la repo (per esempio dopo un clone). Quando poi fai merge su un branch dove il file è shared, le tue modifiche risultano da pubblicare: non si perde niente.

**Se cancelli una repo**, le altre continuano a funzionare. `git shared prune` la toglie dall'elenco dei peer, in tutte le repo; una repo che in quel momento non si riesce a leggere resta. `git shared prune -n` mostra cosa toglierebbe.

**Se sposti una repo**, al primo comando `git shared` l'estensione se ne accorge e ti suggerisce `git shared relocate`, che avvisa gli altri progetti. **Se la copi**, la copia lo dice allo stesso modo: `git shared relocate --copy` la fa diventare un progetto in più, e l'originale resta.

**Se rinomini il file**, usa `git shared mv`. La rinomina vale solo per quella repo.

## Uso in remoto (seconda versione)
Le repo stanno su un servizio di hosting. I peer non sono più path ma **URL**, scritti in `.gitshared`.

**Quando si pubblica:** al `git push`, non al commit. Il commit crea la versione sulla tua macchina; il push la invia al tuo remote e agli URL dei peer. Così lavori anche senza rete, e una versione esce solo insieme al commit che la contiene. Si pubblica solo dai branch su cui il file è shared: il push del branch di una pull request non pubblica niente. Quando la modifica arriva sul branch principale con un merge fatto sul server, la pubblica `git shared publish`, lanciato da chi aggiorna quel branch o dalla CI.

**Cosa arriva ai peer:** solo la notizia che esiste una versione nuova. I loro branch non vengono mai toccati. Chi lavora su quel progetto la applica con `git shared update` e un commit, come in locale.

**Dopo un clone:** hai il file e hai `.gitshared`, ma il collegamento con i peer va rifatto con `git shared link <path del file in un peer>`: riparte dal file che hai già senza toccarlo, e vale subito. La presenza di `.gitshared` ti avvisa che la repo ha file shared, e `git shared status` li mostra come *scollegati*.

**Se non hai il permesso di scrittura su un peer**, quel peer viene saltato. Si aggiornerà da solo al suo prossimo `git shared update`, che va a prendere le versioni dagli altri.

**Se due persone pubblicano nello stesso momento**, ne passa una sola. L'altra riceve un rifiuto, lancia `git shared update` per unire le due versioni e ripubblica. Caso particolare: se le due persone non vedono gli stessi peer (elenchi di URL diversi, permessi diversi), possono passare entrambe. Non si perde niente: le due versioni risultano *divergenti* e il primo `git shared update` le unisce.

| | In locale | In remoto |
|---|---|---|
| I peer sono | path sulla macchina | URL in `.gitshared` |
| Si pubblica | con `git shared publish` | al `git push` |
| Collegamento | `git shared link <path>` | dopo il clone, di nuovo `link` |
| Modifiche contemporanee | rare | normali: una passa, l'altra fa `update` |
| Permessi | nessun problema | serve la scrittura sui peer, altrimenti si aggiornano da soli |

## I comandi
| Comando | Cosa fa |
|---|---|
| `git shared set <file>` | Dichiara shared un file della repo corrente |
| `git shared link <file-in-altra-repo> [<destinazione>]` | Collega alla repo corrente un file shared di un'altra repo |
| `git shared unlink <file>` | Toglie il file dalla condivisione; resta come file normale. Al commit gli altri progetti dimenticano questo |
| `git shared status [--peers]` | Mostra lo stato di ogni file shared; con `--peers` anche se gli altri progetti hanno la stessa versione |
| `git shared update [<file>]` | Prende le versioni dai peer e applica l'ultima |
| `git shared publish [<file>]` | Crea la nuova versione e la invia ai peer |
| `git shared hook install` | Installa gli hook di avviso, per esempio in una repo i cui file shared non li hanno ancora |
| `git shared mv <vecchio> <nuovo>` | Rinomina un file shared |
| `git shared branch [<file>] [-a\|-rm <branch>]` | Mostra o modifica i branch su cui il file è shared (effetto immediato; `-a` e `-rm` sono le forme brevi di `--add` e `--remove`);\|--remove <branch>]` | Mostra o modifica i branch su cui il file è shared (effetto immediato); con `--default` modifica l'elenco di default in `.gitshared`, con `--reset` lo ricopia in locale. Solo nomi esatti |
| `git shared prune [-n]` | Toglie dall'elenco i peer cancellati o che non condividono più il file |
| `git shared relocate [--copy]` | Avvisa i peer che questa repo è stata spostata (o, con `--copy`, copiata); `--peer <vecchio> <nuovo>` corregge il path di un peer spostato |
| `git shared lang <en\|it>` | Sceglie la lingua dei messaggi |
| `git shared <comando> --help` | La pagina di aiuto del comando; `git shared help` elenca i comandi, `git shared <comando> -h` riassume le opzioni |

Gli stati che vedi in `git shared status`:

| Stato | Significato | Cosa fare |
|---|---|---|
| allineato | Hai l'ultima versione | Niente |
| da applicare | Esiste una versione più recente | `git shared update` |
| da committare | Hai applicato l'ultima versione con `update`, ma non hai ancora fatto commit | `git commit`, oppure `git restore --staged --worktree <file>` per annullare |
| da pubblicare | Hai una modifica che i peer non hanno | `git shared publish` |
| divergente | Tu e un peer avete modificato il file separatamente | `git shared update`, che unisce |
| unione in corso | `update` ha trovato un conflitto, oppure l'hai risolto e messo in staging | Correggi il file, `git add`, poi `git shared update`; per rinunciare `git restore -s HEAD -SW <file>` |
| in attesa di commit | Hai lanciato `set`, `link` o `unlink` ma non hai ancora committato | `git commit`, oppure `git restore` per annullare |
| incoerente | Il file è stato rinominato o cancellato fuori da `git shared` | `git shared mv` o `git shared unlink` |
| sospeso | Su questo branch il file non è gestito come shared. Se nessuno dei suoi branch esiste più (es. lo hai rinominato) te lo dicono `status` e gli avvisi | Niente, oppure `git shared branch <file> -a <branch>` |
| scollegato | La repo dichiara il file ma non conosce i peer (clone) | `git shared link` |

## Limiti della prima versione
- Solo uso **in locale**.
- Solo **file di testo** singoli: niente cartelle, niente file binari.
- Linux e Windows. Serve git 2.34 o più recente.
- Al massimo 32 peer per file shared; path fino a 4096 caratteri. Nessun limite al numero di file shared per repo.
- L'estensione va installata su ogni macchina che deve pubblicare o applicare versioni. Chi non ce l'ha usa comunque la repo normalmente.

test shared dell'utente

secondo test