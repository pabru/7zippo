Converting an archive (.zip, for example) to another type (.7z, for example) is a common and trivial task. I face such tasks these days. To my surprise, the standard 7-zip release doesn't include this feature. There have been such requests for severals years on 7-zip's forum, however the author seems not to be interested in adding this feature, so I decide to write it myself.

7zippo makes use of the 7-zip SDK, and is capable of reading all formats that 7-zip supports. But why did I take the .zip format for example? Because (1) converting a zip archive is quite benefitial, and (2) documents such as .docx and .odf are all based on the zip format. I have found that converting these documents to .7z results in much higher compression ratio that directly re-compress them to 7z (.docx.7z or .odf.7z).

7zippo currently is a simple commland line tool, and the project outline is:
  1. ore Functionality (command line only).
  1. ntegrating with Windows Explorer context menu.
  1. ntegrating with Offices to support 7-zipped documents.
  1. ontributing to 7-zip a CODEC specific to these zipped documents that actually performs conversion.