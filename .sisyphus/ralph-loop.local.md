---
active: true
iteration: 1
max_iterations: 500
completion_promise: "DONE"
initial_completion_promise: "DONE"
started_at: "2026-04-17T06:26:41.089Z"
session_id: "ses_265e1410efferQeVHhKcIJivpw"
ultrawork: true
strategy: "continue"
message_count_at_start: 1
---
Enhance the suno integrations into this Qt+QML c++ 23 audio player focused around playing suno remote tracks and creating music videos with optional lyrical karoke as follows:
-Ability to browse the entire library with columns for song details that can be sorted ascending/descending and the active sort/view mode saved in the packages working directory settings in the projects diretories within $HOME. 
-Ability to search for strings, perhaps with an advanced search ability either in a pop-up window from the top menu bar, button, and/or hotkey, to be able to find songs when, like the user has, libraries inolve thousands of songs.
- Optional ability to toggle downloading to a configurable directory when playing and means of the donwloads being sorted in some form of directory and file name structure. 
- Expanding on the download ability means the ability to add id3 tags to the downloaded mp3 or wav files.
- Option to display song cover art over/in the projectm viewport canvas area, plus saving to the songs metadata/tags.
- Lyrics saved to tags, will have to decide if should be raw, time-synced LRC/SRT format, or both.
- Adding the ability to do all operations available on suno incuding deleteing songs, sending to trash, creating playlists, viewing playlists, rating thumbs up/down, and all other means available in the suno API wil eventually be part of this project with it having become more of a suno frontend desktop package primarily. This will also be one of, if not the only, first suno desktop client available in the entire community, oper-source or otherwise. 

Use any of the fairly recent suno.com and suno.ai endpoint scans within anywhere in `/home/nsomnia/` but most of the more recent ones should be found in `~/git/suno-api-and-client-to-database_all/suno-api-and-client-to-database_inital-prompt-template-for-llm-models/docs/` with `~/git/suno-api-and-client-to-database_all/suno-api-and-client-to-database_inital-prompt-template-for-llm-models/docs/all-endpoints/` likely being the most token efficient, as some scans are huge dumps (which may be useful if more specifics are needed) such as the following from `/home/nsomnia/git/suno-api-and-client-to-database_all/suno-api-and-client-to-database_inital-prompt-template-for-llm-models/docs/api-references/`:
```
.rw-r--r--  12M nsomnia 30 Mar 18:27  suno-recon-full-1774916856996.json
.rw-r--r--  19M nsomnia  1 Apr 15:02  sniffing-huge-output.json
.rw-r--r--  21M nsomnia  1 Apr 13:08  site-mape-extractor-site-map-to-file.txt
.rw-r--r--  34M nsomnia  1 Apr 13:12  site-mape-extractor-site-map-to-file-full.txt
.rw-r--r--  65M nsomnia  1 Apr 04:14  burpsuite-mega-scan-all.html
.rw-r--r--  71M nsomnia  1 Apr 03:56  burpsuite-mega-scan_burp-logger.output
.rw-r--r--  74M nsomnia  1 Apr 04:13  all-suno.log.csv
.rw-r--r--  80M nsomnia  1 Apr 03:57  burpsuite-mega-scan_burp-logger.csv 
```

You may write or updated any files needed in the agents working directory under `.agent/` and the TODO.md should have instructions about keeping up to date with progress and any notes for the user plus the ability to refactor fully and add to it so long as no items are removed even when finished with the user then using this knowing what to fully test before removing. You are free to work on other TODO.md items along the way that become relevant to current tasks. 
All documentation should be kept up-to-date as deemed appropraite. 
Commit and push to this qml refator current branch as needed to have a good "changelog" history for future agent coding sessions. 
All code is written and read only by yourself with the user not having adequite c++ skills anymore particularily modern c++ 23. This means lay out code and files in the manner most appropriate for agentic working success for your own model. The codebase may be improved in the future with the newly release GLM-5.1 model of which the user is in active testing its capabilities. Work in ultrawork mode until anything stopping is reached or your uncomforable continuing without the users input or the user investigating something that requires the users attention such as GUI elements. 
Any logic you deem can be improved in anyway should be improved as this is a long running codebase spanning across many LLM generations and familieis.
Your free to search github with tool calls or via `gh search repos
