Rotaru Petru-Alexandru, 321CB

Precizari:
- Am ales sa implementez server-ul in C++ ca sa pot sa folosesc anumite
structuri de date din STL: std::vector si std::unordered_map. Cum nu am
avut nevoie de ele pentru implementarea clientului, acesta a fost implementat
in C.
- Fisierele utils.h, utils.c contin definitiile structurilor de date folosite
pentru implementarea protocoalelor peste UDP si TCP si functii care manipuleaza
aceste structuri de date.
- Am adaugat flag-ul de optimizare -O3 la compilare deoarece acesta nu a fost
interzis din enunt. Am testat local si am trecut toate testele din checker atat
cu acest flag de compilare, cat si fara.

Pentru a diferentia pachetele folosite de cele doua tipuri diferite de clienti,
am numit pachetele primite de la clientii UDP pachete "buletin", iar pachetele
folosite pentru comunicarea cu clientii TCP, pachete "subscriber".
Forma acestora este descrisa de structurile bulletinhdr, respectiv "subhdr",
declarate in utils.h. Acestea sunt folosite atat de server cat si de subscriber.

Structura pachetelor bulletin:
- string continand numele unui topic (50 bytes)
- tipul pachetului trimis (1 byte)
    In implementarea curenta, acest camp poate sa ia valori intre 0 si 3.
    Acest camp este folosit pentru determinarea tipului de date transmis
    in urmatorul camp.
- union peste toate cele 4 tipuri de date care pot fi transmise (1500 bytes)
    Pentru reprezentarea datelor am folosit urmatoarele tipuri:
    sign_int_t - tipul INT
    short_real_t - tipul SHORT_REAL
    real_t - tipul FLOAT
    char[1500] - tipul STRING

Structura este folosita pentru interpretarea mesajelor primite de la
clientii UDP si la transmiterea informatiilor in cadrul pachetelor subscriber.

Structura pachetelor subscriber (Protocol peste TCP):
- lungimea intregului pachet (2 bytes, unsigned)
- string continand id-ul unu client sau server (10 bytes)
- tipul pachetului trimis (1 byte)
    am definit urmatoarele tipuri:
        - SUB_SUB: trimis de clientul TCP pentru a se abona la un topic
        - SUB_UNSUB: trimis de clientul TCP pentru a se dezabona de la un topic
        - SUB_DATA: trimis de server catre clientul TCP pentru a transmite mai
          departe continutul unui pachet bulletin
        - SUB_LOGIN: trimis de clientul TCP imediat dupa stabilirea conexiunii
          pentru a transmite ID-ul sau unic. In cazul in care acest ID este
          conectat deja, serverul va raspunde cu SUB_SERVDOWN.
        - SUB_LOGOUT: trimis de clientul TCP cand doreste sa opreasca conexiunea
          la server
        - SUB_SERVDOWN: trimis de server catre client atunci pentru a-l opri
          imediat. Aceste pachete sunt folosite cand serverul se inchide
          sau serverul doreste sa opreasca conexiunea cu un anumit client.
    Pachetele SUB_LOGIN, SUB_LOGOUT, SUB_SERVDOWN vor contine doar
    primele 3 campuri (vor avea 13 bytes).
- union peste 2 structuri (pana la 1560 bytes):
    struct sub:
        - string continand un topic (50 bytes)
        - store flag (1 byte)
            Acesta va avea valoarea 1 daca clientul doreste sa primeasca
            toate pachetele trimise cat timp era deconectat la urmatoarea
            conectare.
    struct data:
        - port-ul clientului UDP care a trimis informatiile (2 bytes, unsigned)
        - structura in_addr reprezentand IP-ul clinetului UDP (4 bytes)
        - o structura bulletinhdr, reprezentand pachetul primit de server
          de la clientul UDP in intregime.

Comunicarea dintre server si clientii TCP:
       Server                              Subscriber
         |         stabilire conexiune          |
         |<------------------------------------>|
         |                                      |
         |             SUB_LOGIN                |
         |<-------------------------------------|
         |                                      |
         |             SUB_SUB                  | subscribe board 0
         |<-------------------------------------|
         |                                      |

                        .......

board STRING Hello!                             |
-------->|                                      |
(From IP:PORT)                                  |
         |             SUB_DATA                 |
         |------------------------------------->| 
         |                                      | IP:PORT - board - STRING - Hello!
         |                                      |
         |             SUB_LOGOUT               | exit
         |<-------------------------------------| 
         | 
         | 
        ...

Dupa stabilirea unei conexiuni, clientul TCP va trimite un mesaj de log in
serverului. Dupa aceea clientul va trimite mai departe cereri de tip SUB_SUB sau
SUB_UNSUB, iar serverul va trimite mai departe pachetele care contin un topic
la care clientul s-a abonat prin pachete de tip SUB_DATA. Comunicarea dintre
client si server se va opri atunci cand clientul sau serverul vor primi
comanda exit de la stdin. Daca clientul se opreste, el anunta serverul printr-un
pachet SUB_LOGOUT, iar daca serverul se opreste, el va opri toti clientii TCP
conectati prin pachete de tip SUB_SERVDOWN. Serverul se va opri si in cazul in
care intampina o eroare.
