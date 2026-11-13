# Security / Sécurité

## Reporting a vulnerability

Please do not report security problems in a public issue. Use **Security > Report a vulnerability** on this repository, which sends a private report that only the maintainer can read. You will get an answer as soon as possible, and a fix will be released before the details are made public.

## How Camerist protects your setup

- The local server that serves the dock and the Stream Deck addresses only listens on `127.0.0.1`. It cannot be reached from another computer or from the internet.
- Requests coming from another website opened in a browser on the same computer are refused, so a web page cannot move your camera or change your OBS source in the background.
- Camerist does not collect or send any data.

## Signaler une faille

Ne signale pas un problème de sécurité dans un ticket public. Utilise **Security > Report a vulnerability** sur ce dépôt : le signalement reste privé et seul le mainteneur peut le lire. Une correction sera publiée avant que les détails soient rendus publics.
