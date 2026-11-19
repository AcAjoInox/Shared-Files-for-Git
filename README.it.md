🇮🇹 [Leggi in italiano](README.it.md)
🇬🇧 [Read in English](README.md)

# Shared Files for Git

Lo stesso file in più repository git, sempre allineato.

Un header comune, un file di configurazione, uno script: spesso servono a più progetti, e copiarli a mano porta prima o poi a copie diverse. `git shared` è un'estensione di git che tiene **lo stesso file allineato in più repository**. Modifichi il file in un progetto qualsiasi e lo pubblichi; gli altri progetti vengono a sapere che c'è una versione nuova e la adottano quando vogliono, con un comando.

Il file resta un file normale: chi usa la repo senza l'estensione lo vede, lo compila e lo usa come sempre.

> **Stato:** prima versione, tra repository sulla **stessa macchina**. L'uso tra repository remote (GitHub, GitLab) è in sviluppo.

## Installazione

Servono git 2.34 o più recente, `make` e `gcc` (su Windows MinGW-w64).

```bash
make            # compila
make install    # copia git-shared in ~/bin (Windows: %USERPROFILE%\bin)
git shared -h   # verifica
```

Se `git shared` non viene trovato, aggiungi quella cartella al `PATH`. Su Windows, per avere anche `git shared --help`, lancia `make install` da amministratore.

## In due minuti

```bash
# Progetto A: dichiaro shared l'header
cd ~/progetti/A
git shared set include/common.h
git commit -m "common.h diventa shared"

# Progetto B: lo collego (il path può essere diverso)
cd ~/progetti/B
git shared link ~/progetti/A/include/common.h inc/common.h
git commit -m "collego common.h"      # da qui A e B si conoscono

# In B modifico l'header e pubblico
git commit -am "aggiungo MAX_LEN"
git shared publish                    # A riceve un avviso

# In A adotto la versione nuova
cd ~/progetti/A
git shared status                     # common.h: versione nuova da applicare
git shared update
git commit -m "aggiorno common.h"
```

## Come funziona

- **Nessuna repo principale.** Tutte le repo che usano il file (i *peer*) sono alla pari, e tutte possono modificarlo. Non c'è un server: le repo si parlano direttamente.
- **`.gitshared`.** Elenca i file shared della repo. Lo gestisce l'estensione e si versiona con la repo, come `.gitmodules`.
- **Una storia per file.** Ogni file shared ha una sua piccola storia di versioni, uguale in tutti i peer e separata da quella del progetto. Serve a capire quale versione è più recente.
- **Pubblicare, ricevere, applicare.** `publish` crea una versione e avvisa i peer. Ogni peer la riceve da sé al suo primo comando `git shared`, senza toccare i tuoi file. `update` la porta nel progetto, e tu confermi con un commit.

## Le regole

1. **Conta solo ciò che è committato.** Una modifica non committata non si pubblica mai. `set`, `link`, `unlink` e `mv` diventano effettivi al commit: prima puoi annullarli con `git restore`.
2. **Decidi tu quando.** Si pubblica solo con `git shared publish`, e i comandi non fanno commit da soli. Con `-c` o `-m "messaggio"` committano subito, ma solo il file shared e `.gitshared`: il resto dello staging non entra nel commit.
3. **Il tuo lavoro non si perde.** `update` non sovrascrive modifiche non committate, nemmeno quelle in staging. Per forzare serve `-f`.
4. **Nessuna modifica viene scartata.** Se due repo modificano il file ognuna per conto suo, `update` unisce le versioni come `git merge`. In caso di conflitto risolvi i marker, fai `git add` e rilanci `git shared update`. Per rinunciare c'è `git shared update --abort`.
5. **Nessun blocco.** Puoi modificare il file anche se la tua copia è indietro: il file risulta *divergente* e `update` unisce.

## I comandi

| Comando | Cosa fa |
|---|---|
| `git shared set <file>` | Dichiara shared un file della repo |
| `git shared link <file-in-altra-repo> [<dest>]` | Collega un file shared di un'altra repo; una copia che esiste già resta com'è e `update` la allinea |
| `git shared unlink <file>` | Smette di condividere il file, che resta come file normale |
| `git shared mv <file> <dest>` | Rinomina un file shared, solo in questa repo |
| `git shared status [--peers]` | Stato di ogni file shared; con `--peers`, anche quello dei peer |
| `git shared publish [<file>]` | Crea una versione nuova e avvisa i peer |
| `git shared update [--all] [<file>]` | Riceve e applica l'ultima versione; `--abort` rinuncia a un'unione |
| `git shared branch <file> [-a\|-rm <branch>]` | Sceglie i branch su cui il file è shared; sugli altri è un file normale |
| `git shared prune [-n]` | Toglie i peer cancellati o che non condividono più il file |
| `git shared relocate [--copy]` | Avvisa i peer che la repo è stata spostata o copiata |
| `git shared lang [en\|it]` | Lingua del manuale |

Ogni comando ha la sua pagina: `git shared <comando> --help`. Le stesse pagine sono nella cartella [`help/`](help/) (in italiano in [`help/it/`](help/it/)).

## Gli stati

| Stato | Significato | Cosa fare |
|---|---|---|
| allineato | Hai l'ultima versione | Niente |
| da applicare | C'è una versione più recente | `git shared update` |
| da committare | Hai applicato con `update`, manca il commit | `git commit` |
| da pubblicare | Hai una modifica che i peer non hanno | `git shared publish` |
| divergente | Tu e un peer avete modificato il file separatamente | `git shared update` |
| unione in corso | `update` ha trovato un conflitto | Correggi, `git add`, `git shared update` |
| in attesa di commit | `set`, `link`, `unlink` o `mv` non ancora committato | `git commit` o `git restore` |
| sospeso | Su questo branch il file non è shared | Niente, o `git shared branch` |
| scollegato | La repo dichiara il file ma non conosce i peer (es. un clone) | `git shared link` |
| incoerente | File rinominato senza `git shared mv` o cancellato, o `.gitshared` modificato a mano | `git shared mv`, `git shared unlink` o `git restore .gitshared` |

## Limiti della prima versione

- Solo repository sulla stessa macchina (Linux e Windows).
- Solo singoli file di testo: niente cartelle, niente binari.
- Al massimo 32 peer per file.
- L'estensione va installata su ogni macchina che pubblica o applica versioni. Chi non ce l'ha usa la repo normalmente.

## Come è stato fatto

Il codice è stato scritto interamente da un modello di AI, sotto supervisione umana. Idee, requisiti, test e debug sono guidati da una persona.

## Licenza e contatti

Rilasciato con licenza [MIT](LICENSE).

"Shared Files per Git" è un progetto indipendente e non è affiliato né approvato dal progetto Git. Git e il logo di Git sono marchi registrati o marchi di Software Freedom Conservancy, Inc., sede societaria del progetto Git, negli Stati Uniti e/o in altri paesi.

Per domande o segnalazioni: lucamurani@gmail.com
