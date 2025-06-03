# 🔑 Unlocked 🔑

## Introduction

<img src="https://firebasestorage.googleapis.com/v0/b/portfolio-aa70a.appspot.com/o/projects%2Flockwebsitedisplay.webp?alt=media" width="600px" />

### Background

An open source Lock picking robot. Basically, there are many skeleton keys in use (the most common example is the TSA 007, [which is bad](https://www.youtube.com/watch?v=GhESSMvf_to)). Having one key which can open MANY different locks, especially without the lock owners people knowing it exists [or can be bought on Aliexpres for a pound](https://www.aliexpress.com/item/1005006407379398.html). It is a massive security issue. However, the TSA, locksmiths etc need to open locks in certain situations, and if you make locks easy to open for them, they are easy to open for everyone.

### 🔒 ➡️ 🔑🤖 ➡️ 🔓

So, lock-picking robot!!! The idea behind this robot is that it will be able to open difficult-to-lick locks. However, it takes a bit of time. That means you can get a lock which this robot would take 5 min to pick, but would require hundreds of keys (rather than a set of 7), and waiting with a very overt device by a lock for 5 min is a lot more obvious than unlocking it with a key which makes it look like you own the lock. 

### How it works

It feeds wire through a hollow key to press each pin and brute force all possible combinations. So security pins do not affect it since it is not looking for force feedback. However, more pins, or potential cut depths (entropy) make it take exponentially longer, it takes about 0.7s per combo so a 4 pin/4 cut depth lock would take about 3 min, but 5 pin/5 cut depth would take about 35 min to try all combos. So it's possible to design a lock with many security pins and an annoying keyway, very hard to pick for a human, but relatively quick for this robot.

### TLDR:

For lock sports/locksmiths its a fun toy, and for everyone else an interesting alternative to skeleton keys (and stop using TSA locks)

### Demo

<img src="https://github.com/etinaude/unlocked/blob/main/images/githubDemo.webp" width="600px" />



## 💻 Software

### Prerequirements
- Arduino IDE
- Git
- Using [openrb board manager](https://raw.githubusercontent.com/ROBOTIS-GIT/OpenRB-150/master/package_openrb_index.json)
- Arduino SAMD Board Manager
- DYNAMIXEL2Arduino Library

Follow [this guide](https://emanual.robotis.com/docs/en/parts/controller/openrb-150/#development-environment) to get everything set up


The Source Code is in the repo, fully open source, if you make any changes (please do!) keep them open source as well (GPL 3.0 licence)


## ⚡ Electronics

### Components

- Servo Motors - [Dynamixel xl330-m288-t](https://emanual.robotis.com/docs/en/dxl/x/xl330-m288/) 
- Driver Board - [openrb-150](https://emanual.robotis.com/docs/en/parts/controller/openrb-150/)

## ⚙️ Mechanics

Unlocked uses FDM, SLA and DMLS parts for different levels of precision and strength, it uses 0.4mm copper wire to press the pins. More details in the `/3D files` Folder

### Tools Required

- FDM 3D printer
- Vat Polymerization 3D printer (DLP/SLA)
- (Ideally) DMLS 3D printer
- General Hand tools

## 📝 Notes

A new key blank will be needed for a different kind of lock based on the number of pins and the keyway, the current one can be modified to fit this, this should be made out of DMLS steel, companies like JLCPCB can print this, or you can use a SLA part which will likely break a few times so you'll a few spare.

### Future developments

- Include a more robust way to test if the lock is unlocked (for now it tilts slightly from gravity) but a current feedback servo would be better
- Use current feedback to create a set of possible sets or false sets to reduce the number of combos to try
- Before brute force try combing first


## 📄 References

- [Original 1991 Mechanical Patent -  US patent 5,172,578](https://www.researchgate.net/figure/S-A-Bitzioss-1991-design-for-a-Sputnik-decoder-pick-for-pin-tumbler-locks-US_fig9_271020041)
- [A good YouTube video of a design also based on the 1991 patent](https://www.youtube.com/watch?v=QE9MT1LG-PU)
- [Physical lock decoder](https://www.youtube.com/watch?v=CLcOZhq2GjQ)


## 💬 Contact Me

[https://etinaude.dev/contact](https://etinaude.dev/contact)
