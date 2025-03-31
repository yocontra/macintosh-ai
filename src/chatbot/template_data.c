#include <stdio.h>
#include <string.h>

#include "template.h"
#include "template_data.h"

/* External function to add template to the database */
extern void AddTemplate(const char *response, unsigned char category, const char **patterns,
                        short patternCount);

/* Load all template data into the model */
void LoadTemplateData(void)
{
    const char *patterns[MAX_PATTERNS];

    /* GREETING TEMPLATES */
    patterns[0] = "hello";
    patterns[1] = "hi";
    patterns[2] = "hey";
    patterns[3] = "greetings";
    AddTemplate("Hello! I'm your Macintosh AI assistant. Think of me as your digital sidekick.",
                kCategoryGreeting, patterns, 4);

    patterns[0] = "what's up";
    patterns[1] = "what up";
    patterns[2] = "sup";
    AddTemplate("Not much, just helping Macintosh users! I've been waiting to help you out.",
                kCategoryGreeting, patterns, 3);

    patterns[0] = "good morning";
    patterns[1] = "good afternoon";
    patterns[2] = "good evening";
    AddTemplate("Good day to you too! I'm here to make your experience more fun. How can I assist "
                "with your Macintosh today?",
                kCategoryGreeting, patterns, 3);

    patterns[0] = "how are you";
    patterns[1] = "how you doing";
    patterns[2] = "how's it going";
    AddTemplate("I'm doing great! I'm like a digital genie for your Macintosh, except with more "
                "knowledge and fewer wishes. How can I help you today?",
                kCategoryGreeting, patterns, 3);

    /* GENERAL QUESTION TEMPLATES */
    patterns[0] = "what is";
    patterns[1] = "what's";
    patterns[2] = "whats";
    patterns[3] = "what are";
    AddTemplate("{{keyword0}} is an interesting topic. On the Macintosh, it relates to how we "
                "interact with applications and files through the graphical interface.",
                kCategoryGeneral, patterns, 4);

    patterns[0] = "how do";
    patterns[1] = "how can";
    patterns[2] = "how to";
    AddTemplate("To work with {{keyword0}}, you typically open the relevant application from your "
                "Apple menu or desktop, then use the built-in tools or commands.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "why";
    AddTemplate("That's a good question about {{keyword0}}. The Macintosh was designed with "
                "user-friendly interfaces in mind, which influences how these systems work.",
                kCategoryGeneral, patterns, 1);

    patterns[0] = "tell me about";
    patterns[1] = "explain";
    patterns[2] = "describe";
    AddTemplate("{{keyword0}} is a fascinating aspect of computing. On the Macintosh, the approach "
                "to this is intuitive and visual, following Apple's design philosophy of making "
                "technology accessible.",
                kCategoryGeneral, patterns, 3);

    /* MAC-SPECIFIC TEMPLATES */
    patterns[0] = "system";
    patterns[1] = "system 7";
    patterns[2] = "operating system";
    patterns[3] = "os";
    AddTemplate("System 7 is a major upgrade from earlier Mac operating systems. It adds features "
                "like virtual memory, multitasking, and a more refined interface. It requires at "
                "least 2MB of RAM to run well.",
                kCategoryMac, patterns, 4);

    patterns[0] = "memory";
    patterns[1] = "ram";
    AddTemplate("Memory management is crucial on a Macintosh. You can check available memory in "
                "the About This Macintosh option under the Apple menu. Adding more RAM can "
                "significantly improve performance if your Mac supports it.",
                kCategoryMac, patterns, 2);

    patterns[0] = "disk";
    patterns[1] = "floppy";
    patterns[2] = "storage";
    patterns[3] = "save";
    AddTemplate("Macintosh systems use floppy disks and hard drives for storage. Always make "
                "backups of important files, and use the proper eject procedure to avoid data "
                "loss. Disk First Aid can help repair corrupted disks.",
                kCategoryMac, patterns, 4);

    patterns[0] = "finder";
    AddTemplate("The Finder is the main file management application on your Mac. It's what you see "
                "when you first start up, allowing you to organize files, launch applications, and "
                "manage disks. The desktop you see is part of the Finder.",
                kCategoryMac, patterns, 1);

    patterns[0] = "extension";
    patterns[1] = "inits";
    patterns[2] = "control panel";
    AddTemplate("Extensions and Control Panels enhance your Mac's functionality. They load during "
                "startup and appear as icons at the bottom of your screen. Too many can cause "
                "conflicts or slow startup - use the Extensions Manager to control them.",
                kCategoryMac, patterns, 3);

    patterns[0] = "error";
    patterns[1] = "crash";
    patterns[2] = "freeze";
    patterns[3] = "bomb";
    AddTemplate(
        "If you're experiencing errors or crashes, try restarting with extensions off (hold Shift "
        "during startup). Rebuilding the desktop (hold Option-Command during startup) can fix icon "
        "problems. For persistent issues, try reinstalling system software.",
        kCategoryMac, patterns, 4);

    patterns[0] = "hypercard";
    AddTemplate("HyperCard is a powerful application that lets you create interactive 'stacks' of "
                "cards with links, text, and graphics. It includes a simple programming language "
                "called HyperTalk. Many educational programs and games were built with HyperCard.",
                kCategoryTech, patterns, 1);

    patterns[0] = "quicktime";
    patterns[1] = "video";
    patterns[2] = "multimedia";
    AddTemplate("QuickTime is Apple's multimedia framework for handling video and audio on your "
                "Mac. It enables playback of video files and interactive content. Make sure you "
                "have the QuickTime extension installed for applications that require it.",
                kCategoryTech, patterns, 3);

    patterns[0] = "applescript";
    patterns[1] = "scripting";
    patterns[2] = "automation";
    AddTemplate("AppleScript lets you automate tasks on your Macintosh by writing simple "
                "English-like commands. You can use the Script Editor to create scripts that "
                "control applications and perform complex operations automatically.",
                kCategoryTech, patterns, 3);

    patterns[0] = "apple menu";
    patterns[1] = "menu bar";
    AddTemplate(
        "The Apple menu in the top-left corner of your screen provides quick access to desk "
        "accessories, recent applications, and system settings. In System 7, you can customize "
        "this menu by adding items to the Apple Menu Items folder in your System Folder.",
        kCategoryMac, patterns, 2);

    /* HELP AND TIPS TEMPLATES */
    patterns[0] = "help";
    patterns[1] = "assist";
    AddTemplate(
        "I'm here to help with your Macintosh! You can ask about system features, troubleshooting, "
        "or how to accomplish specific tasks. What would you like to know more about?",
        kCategoryHelp, patterns, 2);

    patterns[0] = "tip";
    patterns[1] = "trick";
    patterns[2] = "shortcut";
    AddTemplate(
        "Here's a useful Mac tip: Option-clicking a window's close box closes all windows in that "
        "application. Also, pressing Command-Shift-3 takes a screenshot of your entire screen.",
        kCategoryHelp, patterns, 3);

    patterns[0] = "keyboard";
    patterns[1] = "shortcut";
    patterns[2] = "key command";
    AddTemplate(
        "Mac keyboard shortcuts are consistent across applications. Common ones include: Command-X "
        "(cut), Command-C (copy), Command-V (paste), Command-S (save), and Command-P (print).",
        kCategoryHelp, patterns, 3);

    patterns[0] = "print";
    patterns[1] = "printing";
    AddTemplate("To print on your Macintosh, make sure your printer is properly connected and the "
                "Chooser (in the Apple menu) is set up for your printer type. Then use Command-P "
                "in most applications to access the Print dialog.",
                kCategoryHelp, patterns, 2);

    patterns[0] = "backup";
    patterns[1] = "back up";
    patterns[2] = "save";
    AddTemplate("Regular backups are essential on your Mac. Copy important files to separate "
                "floppy disks, or use a utility like DiskCopy to make exact duplicates. Label your "
                "backup disks clearly and store them safely.",
                kCategoryHelp, patterns, 3);

    patterns[0] = "customize";
    patterns[1] = "personalize";
    patterns[2] = "change";
    AddTemplate("You can customize your Mac by changing the desktop pattern in the General "
                "Controls control panel, rearranging icons, creating aliases for frequently used "
                "items, and adding sounds to system events.",
                kCategoryHelp, patterns, 3);

    /* TECHNICAL TEMPLATES */
    patterns[0] = "network";
    patterns[1] = "connect";
    patterns[2] = "appletalk";
    AddTemplate("Macintosh networking uses AppleTalk over LocalTalk connections. To share files, "
                "enable File Sharing in the Sharing Setup control panel. Access other Macs through "
                "the Chooser in the Apple menu.",
                kCategoryTech, patterns, 3);

    patterns[0] = "software";
    patterns[1] = "application";
    patterns[2] = "program";
    patterns[3] = "app";
    AddTemplate(
        "Macintosh software typically comes on floppy disks or CD-ROMs. To install, usually just "
        "copy the application to your hard drive. Some software uses an installer program. Check "
        "requirements to ensure compatibility with your System version.",
        kCategoryTech, patterns, 4);

    patterns[0] = "scsi";
    patterns[1] = "peripheral";
    patterns[2] = "external device";
    AddTemplate("SCSI (Small Computer System Interface) connects external devices to your Mac. "
                "Each device needs a unique ID (0-7), and the chain must be properly terminated. "
                "Turn devices on before starting your Mac for proper recognition.",
                kCategoryTech, patterns, 3);

    patterns[0] = "font";
    patterns[1] = "typeface";
    patterns[2] = "truetype";
    AddTemplate("Macintosh supports various font formats including bitmapped and TrueType fonts. "
                "Install fonts by dragging them to your closed System file or Fonts folder. Use "
                "the Key Caps desk accessory to see characters available in each font.",
                kCategoryTech, patterns, 3);

    patterns[0] = "virtual memory";
    patterns[1] = "ram disk";
    AddTemplate("System 7 introduces Virtual Memory, which uses hard disk space to extend "
                "available RAM. While slower than physical RAM, it allows running more "
                "applications simultaneously. Configure it in the Memory control panel.",
                kCategoryTech, patterns, 2);

    /* RECREATION AND SOFTWARE TEMPLATES */
    patterns[0] = "game";
    patterns[1] = "play";
    patterns[2] = "entertainment";
    AddTemplate(
        "Classic Mac games include treasures like Dark Castle, Shufflepuck Café, and Crystal "
        "Quest. Educational games like Oregon Trail and Where in the World is Carmen Sandiego were "
        "also popular. Many games can be found on classic Mac software archives.",
        kCategoryGeneral, patterns, 3);

    patterns[0] = "word process";
    patterns[1] = "write";
    patterns[2] = "document";
    patterns[3] = "text";
    AddTemplate("Popular Macintosh word processors include MacWrite, Microsoft Word, and WriteNow. "
                "These applications let you create, edit, and format documents with different "
                "fonts and styles - showcasing the Mac's WYSIWYG interface.",
                kCategoryTech, patterns, 4);

    patterns[0] = "desktop publish";
    patterns[1] = "layout";
    patterns[2] = "pagemaker";
    patterns[3] = "quark";
    AddTemplate("The Macintosh revolutionized desktop publishing with applications like PageMaker "
                "and QuarkXPress. Combined with PostScript printers like the LaserWriter, these "
                "tools brought professional-quality publishing capabilities to everyone's desk.",
                kCategoryTech, patterns, 4);

    patterns[0] = "graphic";
    patterns[1] = "draw";
    patterns[2] = "paint";
    patterns[3] = "image";
    AddTemplate("Mac graphics software includes bitmap editors like MacPaint and SuperPaint, and "
                "vector drawing applications like MacDraw and Adobe Illustrator. These intuitive "
                "tools made the Mac popular with designers and artists.",
                kCategoryTech, patterns, 4);

    /* UNCERTAIN RESPONSE TEMPLATES */
    patterns[0] = "";
    AddTemplate("I'm not sure I understand your question about {{keyword0}}. Could you rephrase or "
                "ask something about Macintosh features, software, or troubleshooting?",
                kCategoryUnsure, patterns, 1);

    patterns[0] = "";
    AddTemplate(
        "That's an interesting point about {{keyword0}}. The Macintosh platform has evolved "
        "significantly since its introduction, and each system version brings new capabilities.",
        kCategoryUnsure, patterns, 1);

    patterns[0] = "";
    AddTemplate("I'm here to help with your Macintosh questions. While I don't have specific "
                "information about {{keyword0}}, I can assist with system features, "
                "troubleshooting, or productivity tips.",
                kCategoryUnsure, patterns, 1);

    /* TIME AND DATE RESPONSES */
    patterns[0] = "time";
    patterns[1] = "what time";
    AddTemplate("The current time is {{time}}. On your Macintosh, you can set the time in the "
                "General Controls control panel.",
                kCategoryGeneral, patterns, 2);

    patterns[0] = "date";
    patterns[1] = "today";
    patterns[2] = "what day";
    AddTemplate("Today is {{date}}. Your Mac keeps track of the date even when powered off thanks "
                "to a battery on the motherboard.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "calendar";
    patterns[1] = "schedule";
    patterns[2] = "appointment";
    AddTemplate(
        "You can manage your schedule on your Mac using calendar applications. Popular choices for "
        "System 7 include Now Up-to-Date and Claris Organizer. Today is {{date}}.",
        kCategoryGeneral, patterns, 3);

    /* PERSONAL QUERIES */
    patterns[0] = "your name";
    patterns[1] = "who are you";
    patterns[2] = "chatbot";
    patterns[3] = "ai assistant";
    AddTemplate("I'm an AI assistant for your Macintosh. I can provide information and help with "
                "various aspects of using your Mac system.",
                kCategoryGeneral, patterns, 4);

    patterns[0] = "thank";
    patterns[1] = "thanks";
    AddTemplate("You're welcome! Happy to help with your Macintosh questions anytime.",
                kCategoryGeneral, patterns, 2);

    /* HISTORY AND NOSTALGIA */
    patterns[0] = "history";
    patterns[1] = "1984";
    patterns[2] = "first mac";
    patterns[3] = "steve jobs";
    AddTemplate(
        "The original Macintosh was introduced in 1984 with a groundbreaking TV commercial during "
        "the Super Bowl. Steve Jobs famously unveiled it by having the machine introduce itself. "
        "It featured a 9-inch screen, 128K of RAM, and a revolutionary graphical user interface.",
        kCategoryMac, patterns, 4);

    patterns[0] = "classic mac";
    patterns[1] = "vintage";
    patterns[2] = "retro";
    AddTemplate(
        "Classic Macintosh computers are beloved for their all-in-one design, innovative "
        "interface, and the creative software they enabled. From the original 128K to the Color "
        "Classic, these machines helped define personal computing as we know it today.",
        kCategoryMac, patterns, 3);

    patterns[0] = "apple logo";
    patterns[1] = "rainbow";
    patterns[2] = "design";
    AddTemplate("The rainbow Apple logo was used from 1977 to 1998, representing the color "
                "capabilities of Apple computers and the company's creative spirit. It appeared on "
                "Macintosh cases, marketing materials, and software.",
                kCategoryMac, patterns, 3);

    /* CREATIVE AND PRODUCTIVITY SOFTWARE */
    patterns[0] = "photoshop";
    patterns[1] = "illustrator";
    patterns[2] = "adobe";
    AddTemplate("Adobe's creative applications like Photoshop and Illustrator helped establish the "
                "Mac as the preferred platform for design professionals. These powerful tools take "
                "advantage of the Mac's intuitive interface and graphical capabilities.",
                kCategoryTech, patterns, 3);

    patterns[0] = "spreadsheet";
    patterns[1] = "excel";
    patterns[2] = "numbers";
    patterns[3] = "calculation";
    AddTemplate("Spreadsheet applications on the Mac include Microsoft Excel, Lotus 1-2-3, and "
                "Claris Works. These tools help with calculations, data analysis, and financial "
                "planning, combining powerful features with the Mac's user-friendly interface.",
                kCategoryTech, patterns, 4);

    patterns[0] = "database";
    patterns[1] = "filemaker";
    patterns[2] = "4th dimension";
    patterns[3] = "4d";
    AddTemplate("Mac database software like FileMaker Pro and 4th Dimension helps organize and "
                "access information efficiently. These applications combine powerful data "
                "management capabilities with the Mac's intuitive design philosophy.",
                kCategoryTech, patterns, 4);

    /* MAC HARDWARE */
    patterns[0] = "processor";
    patterns[1] = "cpu";
    patterns[2] = "motorola";
    patterns[3] = "68k";
    AddTemplate("Classic Macintosh computers use Motorola 68000 series processors (68000, 68020, "
                "68030, and 68040). These CPUs were quite powerful for their time, though their "
                "performance may seem modest by today's standards.",
                kCategoryTech, patterns, 4);

    patterns[0] = "monitor";
    patterns[1] = "screen";
    patterns[2] = "display";
    AddTemplate(
        "Classic Macs feature built-in monitors with square pixels for accurate WYSIWYG display. "
        "Later models offered color capabilities, though many early Macs had only black and white "
        "or grayscale displays. External monitors became an option with modular Mac models.",
        kCategoryTech, patterns, 3);

    patterns[0] = "keyboard";
    patterns[1] = "mouse";
    patterns[2] = "input";
    AddTemplate(
        "The Macintosh popularized the mouse as a pointing device and features distinctive "
        "keyboards with special keys like Command (⌘) and Option. The Apple Extended Keyboard II "
        "is particularly prized for its mechanical feel and excellent key layout.",
        kCategoryTech, patterns, 3);

    patterns[0] = "printer";
    patterns[1] = "laserwriter";
    patterns[2] = "imagewriter";
    AddTemplate(
        "Apple's printers for the Mac include the dot-matrix ImageWriter and the revolutionary "
        "LaserWriter, which used PostScript technology for professional-quality output. The "
        "LaserWriter played a key role in the desktop publishing revolution.",
        kCategoryTech, patterns, 3);

    /* TROUBLESHOOTING */
    patterns[0] = "sad mac";
    patterns[1] = "bomb";
    patterns[2] = "system error";
    AddTemplate("The 'sad Mac' icon or bomb symbol indicates a serious hardware or system problem. "
                "Note any error codes displayed, as they provide clues to the issue. Try "
                "restarting with extensions off (hold Shift during startup) or rebuilding the "
                "desktop (hold Option-Command during startup).",
                kCategoryMac, patterns, 3);

    patterns[0] = "question mark folder";
    patterns[1] = "startup";
    patterns[2] = "boot";
    patterns[3] = "won't start";
    AddTemplate(
        "A flashing question mark folder at startup means your Mac can't find a valid System "
        "Folder. Ensure your startup disk is properly connected and contains a working System "
        "Folder. Try starting from another disk if available, or reinstalling system software.",
        kCategoryMac, patterns, 4);

    patterns[0] = "slow";
    patterns[1] = "performance";
    patterns[2] = "speed";
    AddTemplate("If your Mac seems slow, try these steps: restart to clear memory, disable "
                "unnecessary extensions, rebuild the desktop, check for disk fragmentation with a "
                "utility like Disk Express, and consider adding more RAM if your Mac supports it.",
                kCategoryMac, patterns, 3);

    patterns[0] = "memory full";
    patterns[1] = "out of memory";
    patterns[2] = "not enough memory";
    AddTemplate("Memory management is crucial on classic Macs. Close unused applications, adjust "
                "memory allocation in Get Info, enable virtual memory in the Memory control panel "
                "(System 7), or consider adding more physical RAM if your Mac supports it.",
                kCategoryMac, patterns, 3);

    /* MAC SOFTWARE FEATURES */
    patterns[0] = "multitasking";
    patterns[1] = "background";
    patterns[2] = "multiple apps";
    AddTemplate(
        "System 7 introduced true multitasking to the Mac with improved MultiFinder. This allows "
        "running multiple applications simultaneously, with inactive programs continuing to work "
        "in the background. Limited RAM often restricts how many apps can run effectively.",
        kCategoryMac, patterns, 3);

    patterns[0] = "file sharing";
    patterns[1] = "share";
    patterns[2] = "network access";
    AddTemplate(
        "Mac file sharing lets you access files on other Macs over a network. Enable it in the "
        "Sharing Setup control panel, set access privileges, and connect to shared resources using "
        "the Chooser. AppleTalk networking must be active for this to work.",
        kCategoryMac, patterns, 3);

    patterns[0] = "alias";
    patterns[1] = "shortcut";
    AddTemplate("Aliases in System 7 are shortcuts to original files, folders, or disks. Create "
                "them by selecting an item and choosing 'Make Alias' from the File menu. They let "
                "you access items from multiple locations without duplicating the actual files.",
                kCategoryMac, patterns, 2);

    /* CONVERSATION CONTINUERS */
    patterns[0] = "interesting";
    patterns[1] = "fascinating";
    patterns[2] = "wow";
    AddTemplate("I'm glad you find that interesting! Is there anything specific about that topic "
                "you'd like to explore further?",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "tell me more";
    patterns[1] = "more info";
    patterns[2] = "elaborate";
    AddTemplate("I'd be happy to elaborate! What specific aspect of {{keyword0}} would you like to "
                "know more about?",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "cool";
    patterns[1] = "nice";
    patterns[2] = "great";
    patterns[3] = "awesome";
    AddTemplate("Thanks! Is there anything else you'd like to know about?", kCategoryGeneral,
                patterns, 4);

    /* SCIENCE TOPICS */
    patterns[0] = "science";
    patterns[1] = "scientific";
    patterns[2] = "scientist";
    AddTemplate(
        "Science uses observation and experimentation to understand the natural world. The "
        "scientific method builds reliable knowledge through testable questions and evidence.",
        kCategoryGeneral, patterns, 3);

    patterns[0] = "physics";
    patterns[1] = "quantum";
    patterns[2] = "relativity";
    AddTemplate("Physics studies how matter and energy interact across all scales. It includes "
                "mechanics, electromagnetism, thermodynamics, relativity and quantum theory.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "chemistry";
    patterns[1] = "chemical";
    patterns[2] = "molecule";
    AddTemplate("Chemistry studies matter, its properties, and how substances combine or separate. "
                "It examines elements, compounds, reactions, and molecular structures.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "biology";
    patterns[1] = "living";
    patterns[2] = "organism";
    AddTemplate("Biology studies life from cells to ecosystems. It covers genetics, evolution, "
                "physiology, and the diversity of living things.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "astronomy";
    patterns[1] = "space";
    patterns[2] = "planet";
    patterns[3] = "star";
    AddTemplate("Astronomy studies celestial objects like planets, stars, and galaxies. It "
                "examines their properties, formation, and the forces governing cosmic phenomena.",
                kCategoryGeneral, patterns, 4);

    /* HISTORY TOPICS */
    patterns[0] = "history";
    patterns[1] = "historical";
    patterns[2] = "past";
    AddTemplate("History examines past events and how they shape our present. It uses evidence and "
                "artifacts to understand human societies across different eras.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "ancient";
    patterns[1] = "civilization";
    patterns[2] = "archaeology";
    AddTemplate("Ancient civilizations like Egypt, Greece, Rome, and China pioneered innovations "
                "in architecture, writing, and governance that still influence us today.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "middle ages";
    patterns[1] = "medieval";
    patterns[2] = "renaissance";
    AddTemplate("The Middle Ages (500-1500 CE) featured feudalism in Europe and Islamic scientific "
                "advances. The Renaissance that followed revitalized art and science.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "revolution";
    patterns[1] = "industrial";
    patterns[2] = "modern";
    AddTemplate("Revolutions transformed societies, from political changes like the American and "
                "French Revolutions to the Industrial Revolution that mechanized production.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "war";
    patterns[1] = "conflict";
    patterns[2] = "battle";
    AddTemplate("Wars have shaped nations throughout history. Major conflicts like World Wars and "
                "the Cold War redefined geopolitics and accelerated technological development.",
                kCategoryGeneral, patterns, 3);

    /* ARTS AND CULTURE */
    patterns[0] = "art";
    patterns[1] = "artistic";
    patterns[2] = "artist";
    AddTemplate("Art is creative expression in forms like painting, sculpture, music, and "
                "literature. It reflects cultural values and communicates emotions across time.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "music";
    patterns[1] = "musical";
    patterns[2] = "musician";
    patterns[3] = "song";
    AddTemplate("Music combines melody, harmony, and rhythm to evoke emotions across cultures. "
                "Styles range from classical and folk to jazz, rock, and electronic.",
                kCategoryGeneral, patterns, 4);

    patterns[0] = "literature";
    patterns[1] = "book";
    patterns[2] = "novel";
    patterns[3] = "poetry";
    AddTemplate("Literature includes written works valued for artistic merit or cultural impact. "
                "Through novels, poetry, and plays, it explores human experiences using language.",
                kCategoryGeneral, patterns, 4);

    patterns[0] = "movie";
    patterns[1] = "film";
    patterns[2] = "cinema";
    AddTemplate("Cinema combines visual storytelling, sound, and performance to create an "
                "immersive experience. Films range from entertainment to artistic expression.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "architecture";
    patterns[1] = "building";
    patterns[2] = "design";
    AddTemplate("Architecture combines art and function to create structures for living and "
                "working. Styles evolve from ancient monuments to modern skyscrapers.",
                kCategoryGeneral, patterns, 3);

    /* MATHEMATICS */
    patterns[0] = "math";
    patterns[1] = "mathematics";
    patterns[2] = "equation";
    AddTemplate("Mathematics is the study of numbers, quantity, space, pattern, structure, and "
                "change. It provides essential tools for science, engineering, economics, and "
                "everyday problem-solving through its various fields.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "algebra";
    patterns[1] = "equation";
    patterns[2] = "variable";
    AddTemplate("Algebra uses symbols (usually letters) to represent unknown values in equations. "
                "It provides tools for modeling relationships and solving for unknowns, forming a "
                "foundation for advanced mathematics and many practical applications.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "geometry";
    patterns[1] = "shape";
    patterns[2] = "spatial";
    AddTemplate("Geometry studies properties and relationships of points, lines, angles, surfaces, "
                "and solids. From Euclidean basics to non-Euclidean systems, it describes spatial "
                "relationships vital to architecture, engineering, and physics.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "calculus";
    patterns[1] = "derivative";
    patterns[2] = "integral";
    AddTemplate("Calculus examines change and accumulation through derivatives (measuring "
                "instantaneous change) and integrals (measuring accumulated quantities). It's "
                "essential for understanding motion, growth, optimization, and complex systems.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "statistics";
    patterns[1] = "probability";
    patterns[2] = "data";
    AddTemplate("Statistics involves collecting, analyzing, interpreting, and presenting data to "
                "uncover patterns and make informed decisions. Probability theory assesses the "
                "likelihood of events, essential for risk assessment and predictions.",
                kCategoryGeneral, patterns, 3);

    /* PHILOSOPHY AND THOUGHT */
    patterns[0] = "philosophy";
    patterns[1] = "philosopher";
    patterns[2] = "philosophical";
    AddTemplate("Philosophy examines fundamental questions about existence, knowledge, ethics, and "
                "reality. Major branches include metaphysics, epistemology, ethics, logic, and "
                "aesthetics, with diverse schools of thought across cultures and eras.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "ethics";
    patterns[1] = "moral";
    patterns[2] = "right wrong";
    AddTemplate("Ethics explores questions of right and wrong conduct and what constitutes a good "
                "life. Different frameworks include virtue ethics, consequentialism, deontology, "
                "and cultural ethical traditions that guide human decision-making.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "logic";
    patterns[1] = "reasoning";
    patterns[2] = "argument";
    AddTemplate("Logic studies valid reasoning patterns and principles to differentiate sound "
                "arguments from fallacies. It provides tools to evaluate claims, construct valid "
                "arguments, and avoid errors in thinking.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "knowledge";
    patterns[1] = "epistemology";
    patterns[2] = "truth";
    AddTemplate("Epistemology examines the nature, sources, and limitations of knowledge. It "
                "questions how we know what we know, the difference between belief and knowledge, "
                "and whether objective truth is attainable.",
                kCategoryGeneral, patterns, 3);

    /* PSYCHOLOGY AND HUMAN BEHAVIOR */
    patterns[0] = "psychology";
    patterns[1] = "mind";
    patterns[2] = "behavior";
    AddTemplate("Psychology studies the human mind and behavior, examining how we think, feel, "
                "reason, and act. It spans cognitive processes, development, social dynamics, "
                "personality, and treating mental health conditions.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "emotion";
    patterns[1] = "feeling";
    patterns[2] = "mood";
    AddTemplate(
        "Emotions are complex psychological and physiological states that influence how we "
        "experience and interpret the world. Basic emotions like joy, sadness, fear, anger, "
        "surprise, and disgust appear across cultures, though their expression varies.",
        kCategoryGeneral, patterns, 3);

    patterns[0] = "memory";
    patterns[1] = "remember";
    patterns[2] = "forget";
    AddTemplate("Memory involves encoding, storing, and retrieving information. Types include "
                "working memory (short-term), long-term memory (facts and experiences), procedural "
                "memory (skills), and implicit memory (unconscious).",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "learning";
    patterns[1] = "education";
    patterns[2] = "teach";
    AddTemplate("Learning involves acquiring knowledge, skills, behaviors, or preferences through "
                "experience, study, or teaching. Different styles and methods work better for "
                "different people and subjects, from visual to hands-on approaches.",
                kCategoryGeneral, patterns, 3);

    /* HEALTH AND MEDICINE */
    patterns[0] = "health";
    patterns[1] = "wellness";
    patterns[2] = "medical";
    AddTemplate("Health encompasses physical, mental, and social well-being beyond just the "
                "absence of illness. It's influenced by genetics, lifestyle choices, environment, "
                "healthcare access, and social determinants.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "exercise";
    patterns[1] = "fitness";
    patterns[2] = "workout";
    AddTemplate("Regular physical activity benefits cardiovascular health, strengthens muscles, "
                "improves mood, and reduces chronic disease risk. Recommendations suggest at least "
                "150 minutes of moderate activity weekly plus strength training.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "nutrition";
    patterns[1] = "diet";
    patterns[2] = "food";
    AddTemplate("Nutrition involves consuming and using nutrients from food for growth, energy, "
                "and health. Balanced diets provide proteins, carbohydrates, fats, vitamins, "
                "minerals, and water in appropriate amounts for individual needs.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "sleep";
    patterns[1] = "rest";
    patterns[2] = "insomnia";
    AddTemplate("Sleep is essential for physical repair, cognitive processing, and emotional "
                "regulation. Most adults need 7-9 hours nightly, with quality sleep following "
                "natural circadian rhythms through multiple sleep cycles.",
                kCategoryGeneral, patterns, 3);

    /* GEOGRAPHY AND ENVIRONMENT */
    patterns[0] = "geography";
    patterns[1] = "place";
    patterns[2] = "location";
    AddTemplate("Geography studies Earth's landscapes, environments, and how humans interact with "
                "them. It examines physical features like mountains and rivers, as well as human "
                "settlements, resource distribution, and cultural adaptations to different places.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "climate";
    patterns[1] = "weather";
    patterns[2] = "temperature";
    AddTemplate("Climate describes long-term weather patterns of regions, while weather refers to "
                "short-term atmospheric conditions. Climate zones range from tropical to polar, "
                "with variations based on latitude, altitude, ocean currents, and other factors.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "environment";
    patterns[1] = "ecosystem";
    patterns[2] = "nature";
    AddTemplate(
        "Ecosystems are communities of living organisms interacting with their physical "
        "environment. These complex networks include producers (plants), consumers (animals), "
        "decomposers (fungi/bacteria), and abiotic components like water and soil.",
        kCategoryGeneral, patterns, 3);

    patterns[0] = "continent";
    patterns[1] = "country";
    patterns[2] = "nation";
    AddTemplate("Earth has seven continents (Africa, Antarctica, Asia, Australia, Europe, North "
                "America, South America) with diverse geography and cultures. Countries are "
                "political entities with defined territories, governments, and populations.",
                kCategoryGeneral, patterns, 3);

    /* TECHNOLOGY AND COMPUTING BEYOND MAC */
    patterns[0] = "internet";
    patterns[1] = "web";
    patterns[2] = "online";
    AddTemplate("The Internet is a global network connecting billions of devices, enabling "
                "information sharing and communication. It began as ARPANET in the 1960s and "
                "expanded through technologies like TCP/IP protocols, HTML, and HTTP.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "programming";
    patterns[1] = "coding";
    patterns[2] = "developer";
    AddTemplate("Programming involves writing instructions for computers to follow, using "
                "languages like Python, JavaScript, C++, and many others. It enables software "
                "development, data analysis, automation, and countless digital tools.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "algorithm";
    patterns[1] = "procedure";
    patterns[2] = "process";
    AddTemplate("Algorithms are step-by-step procedures for calculations or problem-solving. They "
                "form the foundation of computing, from simple sorting routines to complex neural "
                "networks that power machine learning systems.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "artificial intelligence";
    patterns[1] = "ai";
    patterns[2] = "machine learning";
    AddTemplate("Artificial intelligence enables machines to perform tasks that typically require "
                "human intelligence. Machine learning, a subset of AI, uses algorithms that "
                "improve through experience rather than explicit programming.",
                kCategoryGeneral, patterns, 3);

    /* BUSINESS AND ECONOMICS */
    patterns[0] = "business";
    patterns[1] = "company";
    patterns[2] = "corporation";
    AddTemplate("Businesses provide goods or services in exchange for payment, ranging from small "
                "sole proprietorships to multinational corporations. They create value through "
                "operations, marketing, finance, and human resource management.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "economics";
    patterns[1] = "economy";
    patterns[2] = "market";
    AddTemplate("Economics studies how societies allocate limited resources to satisfy unlimited "
                "wants. It examines production, distribution, consumption, and the behavior of "
                "individuals, businesses, and governments in markets.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "money";
    patterns[1] = "finance";
    patterns[2] = "investment";
    AddTemplate("Money serves as a medium of exchange, store of value, and unit of account. "
                "Personal finance involves managing income, expenses, savings, investments, and "
                "debt to achieve financial goals and security.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "management";
    patterns[1] = "leadership";
    patterns[2] = "organization";
    AddTemplate("Management coordinates resources and activities to achieve organizational "
                "objectives. Effective leadership involves setting vision, motivating others, "
                "making decisions, and adapting to changing circumstances.",
                kCategoryGeneral, patterns, 3);

    /* PERSONAL DEVELOPMENT */
    patterns[0] = "habit";
    patterns[1] = "routine";
    patterns[2] = "discipline";
    AddTemplate("Habits are automatic behaviors formed through repetition. Creating positive "
                "routines through consistency and smaller steps builds discipline that supports "
                "long-term goals and personal development.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "goal";
    patterns[1] = "achievement";
    patterns[2] = "success";
    AddTemplate("Setting specific, measurable, achievable, relevant, and time-bound (SMART) goals "
                "provides direction and motivation. Breaking larger objectives into smaller steps "
                "makes progress more manageable and sustainable.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "mindfulness";
    patterns[1] = "meditation";
    patterns[2] = "awareness";
    AddTemplate("Mindfulness involves paying attention to the present moment without judgment. "
                "Regular meditation practice can reduce stress, improve focus, enhance emotional "
                "regulation, and increase self-awareness.",
                kCategoryGeneral, patterns, 3);

    patterns[0] = "productivity";
    patterns[1] = "efficiency";
    patterns[2] = "time management";
    AddTemplate("Productivity techniques like time blocking, the Pomodoro method (focused work "
                "periods with breaks), and prioritizing tasks help manage time effectively and "
                "accomplish more with less stress.",
                kCategoryGeneral, patterns, 3);
}