// Internationalization (i18n) string tables and OS locale detection for 13 languages:
// English, Chinese, Spanish, Hindi, Arabic, Portuguese, Indonesian,
// French, Japanese, Russian, Serbian, Croatian, Bosnian.
//
// Menu items keep Win32 `&` mnemonic accelerators; keyboard shortcut suffixes
// (e.g., "\tCtrl+O") are appended once by the menu builder as a single source of truth.

#include "openrar/i18n.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif

namespace openrar::i18n {

namespace {

std::atomic<int> g_lang{static_cast<int>(Language::En)};

constexpr size_t kLangCount = static_cast<size_t>(Language::Count);
constexpr size_t kKeyCount = static_cast<size_t>(StrKey::Count);

using Table = std::array<const char*, kKeyCount>;

// Order must match StrKey enum in include/openrar/i18n.hpp exactly.
constexpr Table kEn = {
    "&File", "&Commands", "&Tools", "&Favorites", "&Options", "&Language", "&Help",
    "&Open archive...", "Browse &folder...", "Set default &password...", "E&xit",
    "&Add files to archive...", "&Extract to the specified folder", "&Test archived files",
    "&View file", "&Delete files", "Show &information", "Add archive &comment...",
    "&Wizard...", "&Find files...", "&Repair archive", "Convert archive to &SFX (.exe)", "&Benchmark",
    "&Add to Favorites", "&Integrate into Windows Explorer right-click menu", "&Remove Windows Explorer right-click menu", "&About OpenRAR...",
    "Add", "Extract To", "Test", "View", "Delete", "Find", "Wizard", "Info", "Repair",
    "Name", "Size", "Type", "Modified", "Packed", "CRC32", "System Folder", "File folder", "File",
    "Extract &Here", "&Extract files...", "&Refresh",
    "Total", "Selected", "bytes in", "file", "files", "Password set",
    "OK", "Cancel", "Browse...", "Next >",
    "Archive name and parameters", "Archive name:", "Archive format:", "Compression method:", "Set password (AES-256):",
    "Split to volumes, size (e.g. 10m):", "Create solid archive", "Put recovery record (5%)", "Encrypt file names", "Archive comment:",
    "Extraction path and options", "Destination path:", "Overwrite mode:", "Overwrite without prompt", "Skip existing files", "Rename automatically",
    "Options:", "Preserve full paths", "Open folder when done", "Password (if encrypted):",
    "Wizard: Select Operation - OpenRAR", "What do you want to do?", "Unpack an archive", "Create a new archive", "Add files to an already existing archive",
    "Enter password:", "Are you sure you want to delete the selected item(s)?",
    "Registered Windows Explorer cascading 'OpenRAR' right-click menu!", "Removed Windows Explorer 'OpenRAR' right-click menu."
};

constexpr Table kZh = {
    "文件(&F)", "命令(&C)", "工具(&T)", "收藏夹(&A)", "选项(&O)", "语言(&L)", "帮助(&H)",
    "打开压缩包(&O)...", "浏览文件夹(&F)...", "设置默认密码(&P)...", "退出(&X)",
    "添加文件到压缩包(&A)...", "解压到指定文件夹(&E)", "测试压缩文件(&T)",
    "查看文件(&V)", "删除文件(&D)", "显示信息(&I)", "添加压缩包注释(&M)...",
    "向导(&W)...", "查找文件(&F)...", "修复压缩包(&R)", "转换为自解压文件(&S) (.exe)", "基准测试(&B)",
    "添加到收藏夹(&A)", "集成到 Windows 资源管理器右键菜单", "移除 Windows 资源管理器右键菜单", "关于 OpenRAR(&A)...",
    "添加", "解压到", "测试", "查看", "删除", "查找", "向导", "信息", "修复",
    "名称", "大小", "类型", "修改时间", "压缩后", "CRC32", "系统文件夹", "文件夹", "文件",
    "解压到当前文件夹(&H)", "解压文件(&E)...", "刷新(&R)",
    "总计", "已选择", "字节，共", "个文件", "个文件", "已设密码",
    "确定", "取消", "浏览...", "下一步 >",
    "压缩包名称和参数", "压缩包名称:", "压缩包格式:", "压缩方式:", "设置密码 (AES-256):",
    "切分为分卷大小 (如 10m):", "创建固实压缩包", "添加恢复记录 (5%)", "加密文件名", "压缩包注释:",
    "解压路径和选项", "目标路径:", "覆盖方式:", "直接覆盖现有文件", "跳过现有文件", "自动重命名",
    "选项:", "保留完整路径", "完成后打开文件夹", "密码 (如果加密):",
    "向导: 选择操作 - OpenRAR", "您想要做什么?", "解压压缩包", "创建新的压缩包", "向已有压缩包添加文件",
    "请输入密码:", "确定要删除所选项目吗?",
    "已成功注册 Windows 资源管理器 OpenRAR 右键菜单!", "已移除 Windows 资源管理器 OpenRAR 右键菜单。"
};

constexpr Table kEs = {
    "&Archivo", "&Órdenes", "&Herramientas", "&Favoritos", "&Opciones", "&Idioma", "A&yuda",
    "&Abrir archivo...", "Explorar &carpeta...", "Establecer &contraseña...", "&Salir",
    "&Añadir ficheros al archivo...", "&Extraer en la carpeta especificada", "&Comprobar ficheros",
    "&Ver fichero", "&Eliminar ficheros", "Mostrar &información", "Añadir &comentario...",
    "&Asistente...", "&Buscar ficheros...", "&Reparar archivo", "Convertir a &SFX (.exe)", "&Prueba de velocidad",
    "&Añadir a Favoritos", "&Integrar en el menú contextual de Windows", "&Quitar del menú contextual de Windows", "&Acerca de OpenRAR...",
    "Añadir", "Extraer en", "Probar", "Ver", "Eliminar", "Buscar", "Asistente", "Info", "Reparar",
    "Nombre", "Tamaño", "Tipo", "Modificado", "Comprimido", "CRC32", "Carpeta del sistema", "Carpeta", "Archivo",
    "Extraer &aquí", "&Extraer ficheros...", "&Actualizar",
    "Total", "Seleccionado", "bytes en", "archivo", "archivos", "Contraseña activa",
    "Aceptar", "Cancelar", "Examinar...", "Siguiente >",
    "Nombre de archivo y parámetros", "Nombre del archivo:", "Formato de archivo:", "Método de compresión:", "Contraseña (AES-256):",
    "Partir en volúmenes (ej. 10m):", "Crear archivo sólido", "Añadir registro de recuperación (5%)", "Cifrar nombres de fichero", "Comentario:",
    "Ruta de extracción y opciones", "Carpeta de destino:", "Modo de sobrescritura:", "Sobrescribir sin preguntar", "Omitir existentes", "Renombrar automáticamente",
    "Opciones:", "Conservar rutas completas", "Abrir carpeta al terminar", "Contraseña (si está cifrado):",
    "Asistente: Seleccionar operación - OpenRAR", "¿Qué desea hacer?", "Descomprimir un archivo", "Crear un nuevo archivo", "Añadir ficheros a un archivo existente",
    "Introduzca la contraseña:", "¿Está seguro de que desea eliminar los elementos seleccionados?",
    "¡Menú contextual de OpenRAR registrado en el Explorador de Windows!", "Menú contextual de OpenRAR eliminado."
};

constexpr Table kHi = {
    "फ़ाइल(&F)", "कमांड(&C)", "उपकरण(&T)", "पसंदीदा(&A)", "विकल्प(&O)", "भाषा(&L)", "सहायता(&H)",
    "आर्काइव खोलें(&O)...", "फ़ोल्डर चुनें(&F)...", "डिफ़ॉल्ट पासवर्ड(&P)...", "बाहर निकलें(&X)",
    "आर्काइव में जोड़ें(&A)...", "निर्दिष्ट फ़ोल्डर में निकालें(&E)", "आर्काइव जाँचें(&T)",
    "फ़ाइल देखें(&V)", "हटाएं(&D)", "जानकारी दिखाएं(&I)", "टिप्पणी जोड़ें(&M)...",
    "विज़ार्ड(&W)...", "फ़ाइलें खोजें(&F)...", "आर्काइव सुधारें(&R)", "SFX (.exe) बनाएं(&S)", "बेंचमार्क(&B)",
    "पसंदीदा में जोड़ें(&A)", "Windows Explorer राइट-क्लिक मेनू जोड़ें", "Windows Explorer राइट-क्लिक मेनू हटाएं", "OpenRAR के बारे में(&A)...",
    "जोड़ें", "निकालें", "जाँचें", "देखें", "हटाएं", "खोजें", "विज़ार्ड", "जानकारी", "सुधारें",
    "नाम", "आकार", "प्रकार", "संशोधित", "संपीड़ित", "CRC32", "सिस्टम फ़ोल्डर", "फ़ोल्डर", "फ़ाइल",
    "यहाँ निकालें(&H)", "फ़ाइलें निकालें(&E)...", "रीफ़्रेश(&R)",
    "कुल", "चयनित", "बाइट्स,", "फ़ाइल", "फ़ाइलें", "पासवर्ड सेट",
    "ठीक है", "रद्द करें", "ब्राउज़...", "अगला >",
    "आर्काइव नाम और विकल्प", "आर्काइव नाम:", "आर्काइव प्रारूप:", "संपीड़न विधि:", "पासवर्ड (AES-256):",
    "वॉल्यूम में विभाजित करें (जैसे 10m):", "सॉलिड आर्काइव बनाएं", "रिकवरी रिकॉर्ड जोड़ें (5%)", "फ़ाइल नाम एन्क्रिप्ट करें", "आर्काइव टिप्पणी:",
    "निष्कर्षण पथ और विकल्प", "गंतव्य पथ:", "ओवरराइट मोड:", "बिना पूछे बदलें", "मौजूदा फ़ाइलें छोड़ें", "स्वतः नाम बदलें",
    "विकल्प:", "पूर्ण पथ सुरक्षित रखें", "पूर्ण होने पर फ़ोल्डर खोलें", "पासवर्ड (यदि एन्क्रिप्टेड है):",
    "विज़ार्ड: कार्य चुनें - OpenRAR", "आप क्या करना चाहते हैं?", "आर्काइव अनपैक करें", "नया आर्काइव बनाएं", "मौजूदा आर्काइव में फ़ाइलें जोड़ें",
    "पासवर्ड दर्ज करें:", "क्या आप वाकई चयनित आइटम हटाना चाहते हैं?",
    "Windows Explorer राइट-क्लिक मेनू पंजीकृत हो गया!", "Windows Explorer राइट-क्लिक मेनू हटा दिया गया।"
};

constexpr Table kAr = {
    "ملف(&F)", "أوامر(&C)", "أدوات(&T)", "المفضلة(&A)", "خيارات(&O)", "اللغة(&L)", "مساعدة(&H)",
    "فتح أرشيف(&O)...", "تصفح مجلد(&F)...", "تعيين كلمة المرور(&P)...", "خروج(&X)",
    "إضافة إلى الأرشيف(&A)...", "استخراج إلى المجلد المحدد(&E)", "اختبار الملفات(&T)",
    "عرض الملف(&V)", "حذف الملفات(&D)", "عرض المعلومات(&I)", "إضافة تعليق(&M)...",
    "المعالج(&W)...", "البحث عن ملفات(&F)...", "إصلاح الأرشيف(&R)", "تحويل إلى SFX (.exe)(&S)", "اختبار الأداء(&B)",
    "إضافة إلى المفضلة(&A)", "إضافة إلى قائمة الزر الأيمن في Windows", "إزالة من قائمة الزر الأيمن في Windows", "حول OpenRAR(&A)...",
    "إضافة", "استخراج", "اختبار", "عرض", "حذف", "بحث", "معالج", "معلومات", "إصلاح",
    "الاسم", "الحجم", "النوع", "تاريخ التعديل", "مضغوط", "CRC32", "مجلد النظام", "مجلد", "ملف",
    "استخراج هنا(&H)", "استخراج الملفات(&E)...", "تحديث(&R)",
    "الإجمالي", "المحدد", "بايت في", "ملف", "ملفات", "تم تعيين كلمة المرور",
    "موافق", "إلغاء", "استعراض...", "التالي >",
    "اسم الأرشيف والخيارات", "اسم الأرشيف:", "تنسيق الأرشيف:", "طريقة الضغط:", "كلمة المرور (AES-256):",
    "تقسيم إلى أجزاء (مثل 10m):", "إنشاء أرشيف صلب", "إضافة سجل استرداد (5%)", "تشفير أسماء الملفات", "تعليق الأرشيف:",
    "مسار الاستخراج والخيارات", "مسار الوجهة:", "وضع الاستبدال:", "الاستبدال بدون سؤال", "تخطي الملفات الموجودة", "إعادة التسمية تلقائياً",
    "خيارات:", "الحفاظ على المسارات الكاملة", "فتح المجلد عند الانتهاء", "كلمة المرور (إن وجدت):",
    "المعالج: اختر العملية - OpenRAR", "ماذا تريد أن تفعل؟", "فك ضغط أرشيف", "إنشاء أرشيف جديد", "إضافة ملفات إلى أرشيف موجود",
    "أدخل كلمة المرور:", "هل أنت متأكد من حذف العناصر المحددة؟",
    "تم تسجيل قائمة OpenRAR في مستكشف Windows!", "تمت إزالة قائمة OpenRAR من مستكشف Windows."
};

constexpr Table kPt = {
    "&Arquivo", "&Comandos", "&Ferramentas", "&Favoritos", "&Opções", "&Idioma", "A&juda",
    "&Abrir arquivo...", "Procurar &pasta...", "Definir &senha padrão...", "&Sair",
    "&Adicionar arquivos ao arquivo...", "&Extrair para a pasta especificada", "&Testar arquivos",
    "&Visualizar arquivo", "&Excluir arquivos", "Mostrar &informações", "Adicionar &comentário...",
    "&Assistente...", "&Localizar arquivos...", "&Reparar arquivo", "Converter para &SFX (.exe)", "&Benchmark",
    "&Adicionar aos Favoritos", "&Integrar ao menu de contexto do Windows", "&Remover do menu de contexto do Windows", "&Sobre o OpenRAR...",
    "Adicionar", "Extrair", "Testar", "Ver", "Excluir", "Localizar", "Assistente", "Info", "Reparar",
    "Nome", "Tamanho", "Tipo", "Modificado", "Compactado", "CRC32", "Pasta do sistema", "Pasta", "Arquivo",
    "Extrair &aqui", "&Extrair arquivos...", "&Atualizar",
    "Total", "Selecionado", "bytes em", "arquivo", "arquivos", "Senha definida",
    "OK", "Cancelar", "Procurar...", "Avançar >",
    "Nome do arquivo e parâmetros", "Nome do arquivo:", "Formato do arquivo:", "Método de compressão:", "Senha (AES-256):",
    "Dividir em volumes (ex. 10m):", "Criar arquivo sólido", "Adicionar registro de recuperação (5%)", "Criptografar nomes", "Comentário:",
    "Caminho de extração e opções", "Pasta de destino:", "Modo de substituição:", "Substituir sem perguntar", "Ignorar existentes", "Renomear automaticamente",
    "Opções:", "Preservar caminhos completos", "Abrir pasta ao concluir", "Senha (se criptografado):",
    "Assistente: Selecionar operação - OpenRAR", "O que você deseja fazer?", "Descompactar um arquivo", "Criar um novo arquivo", "Adicionar arquivos a um arquivo existente",
    "Digite a senha:", "Tem certeza de que deseja excluir os itens selecionados?",
    "Menu de contexto OpenRAR registrado no Windows Explorer!", "Menu de contexto OpenRAR removido."
};

constexpr Table kId = {
    "&Berkas", "&Perintah", "&Alat", "&Favorit", "&Opsi", "&Bahasa", "&Bantuan",
    "&Buka arsip...", "Jelajahi &folder...", "Atur &sandi default...", "&Keluar",
    "&Tambah ke arsip...", "&Ekstrak ke folder tertentu", "&Uji file arsip",
    "&Lihat file", "&Hapus file", "Tampilkan &info", "Komentar &arsip...",
    "&Panduan...", "&Cari file...", "&Perbaiki arsip", "Konversi ke &SFX (.exe)", "&Benchmark",
    "&Tambah ke Favorit", "&Integrasikan ke menu klik kanan Windows", "&Hapus dari menu klik kanan Windows", "&Tentang OpenRAR...",
    "Tambah", "Ekstrak", "Uji", "Lihat", "Hapus", "Cari", "Panduan", "Info", "Perbaiki",
    "Nama", "Ukuran", "Tipe", "Diubah", "Terkompresi", "CRC32", "Folder Sistem", "Folder", "Berkas",
    "Ekstrak &di sini", "&Ekstrak file...", "&Segarkan",
    "Total", "Dipilih", "byte dalam", "berkas", "berkas", "Sandi aktif",
    "OK", "Batal", "Telusuri...", "Lanjut >",
    "Nama arsip dan parameter", "Nama arsip:", "Format arsip:", "Metode kompresi:", "Atur sandi (AES-256):",
    "Pisah ke volume (mis. 10m):", "Buat arsip solid", "Tambah rekaman pemulihan (5%)", "Enkripsi nama file", "Komentar arsip:",
    "Lokasi ekstraksi dan opsi", "Folder tujuan:", "Mode timpa:", "Timpa tanpa konfirmasi", "Lewati file yang ada", "Ubah nama otomatis",
    "Opsi:", "Pertahankan jalur lengkap", "Buka folder setelah selesai", "Sandi (jika terenkripsi):",
    "Panduan: Pilih Operasi - OpenRAR", "Apa yang ingin Anda lakukan?", "Ekstrak arsip", "Buat arsip baru", "Tambah file ke arsip yang ada",
    "Masukkan sandi:", "Yakin ingin menghapus item yang dipilih?",
    "Menu klik kanan OpenRAR terdaftar di Windows Explorer!", "Menu klik kanan OpenRAR telah dihapus."
};

constexpr Table kFr = {
    "&Fichier", "&Commandes", "&Outils", "&Favoris", "&Options", "&Langue", "&Aide",
    "&Ouvrir une archive...", "Parcourir un &dossier...", "Mot de &passe par défaut...", "&Quitter",
    "&Ajouter à l'archive...", "&Extraire vers le dossier spécifié", "&Tester les fichiers",
    "&Afficher le fichier", "&Supprimer les fichiers", "Afficher les &infos", "Commentaire d'&archive...",
    "&Assistant...", "&Rechercher...", "&Réparer l'archive", "Convertir en &SFX (.exe)", "&Benchmark",
    "&Ajouter aux Favoris", "&Intégrer au menu contextuel Windows", "&Retirer du menu contextuel Windows", "&À propos d'OpenRAR...",
    "Ajouter", "Extraire", "Tester", "Voir", "Supprimer", "Chercher", "Assistant", "Infos", "Réparer",
    "Nom", "Taille", "Type", "Modifié", "Compressé", "CRC32", "Dossier système", "Dossier", "Fichier",
    "Extraire &ici", "&Extraire les fichiers...", "&Actualiser",
    "Total", "Sélectionné", "octets dans", "fichier", "fichiers", "Mot de passe défini",
    "OK", "Annuler", "Parcourir...", "Suivant >",
    "Nom de l'archive et paramètres", "Nom de l'archive :", "Format d'archive :", "Méthode de compression :", "Mot de passe (AES-256) :",
    "Découper en volumes (ex. 10m) :", "Créer une archive solide", "Ajouter enregistrement de récupération (5%)", "Chiffrer les noms de fichiers", "Commentaire :",
    "Chemin d'extraction et options", "Dossier de destination :", "Mode d'écrasement :", "Remplacer sans confirmation", "Ignorer les fichiers existants", "Renommer automatiquement",
    "Options :", "Conserver les chemins complets", "Ouvrir le dossier à la fin", "Mot de passe (si chiffré) :",
    "Assistant : Choisir l'opération - OpenRAR", "Que voulez-vous faire ?", "Décompresser une archive", "Créer une nouvelle archive", "Ajouter des fichiers à une archive existante",
    "Entrez le mot de passe :", "Voulez-vous vraiment supprimer les éléments sélectionnés ?",
    "Menu contextuel OpenRAR ajouté à l'Explorateur Windows !", "Menu contextuel OpenRAR supprimé."
};

constexpr Table kJa = {
    "ファイル(&F)", "コマンド(&C)", "ツール(&T)", "お気に入り(&A)", "オプション(&O)", "言語(&L)", "ヘルプ(&H)",
    "書庫を開く(&O)...", "フォルダを開く(&F)...", "既定のパスワード(&P)...", "終了(&X)",
    "書庫に追加(&A)...", "指定フォルダに展開(&E)", "書庫をテスト(&T)",
    "ファイルを表示(&V)", "削除(&D)", "情報を表示(&I)", "書庫コメント(&M)...",
    "ウィザード(&W)...", "ファイルを検索(&F)...", "書庫を修復(&R)", "自己解凍書庫(&S) (.exe)", "ベンチマーク(&B)",
    "お気に入りに追加(&A)", "Windows 右クリックメニューに登録", "Windows 右クリックメニューから削除", "OpenRAR について(&A)...",
    "追加", "展開", "テスト", "表示", "削除", "検索", "ウィザード", "情報", "修復",
    "名前", "サイズ", "種類", "更新日時", "圧縮後", "CRC32", "システム フォルダ", "フォルダ", "ファイル",
    "ここに展開(&H)", "ファイルを展開(&E)...", "更新(&R)",
    "合計", "選択中", "バイト /", "ファイル", "ファイル", "パスワード設定済",
    "OK", "キャンセル", "参照...", "次へ >",
    "書庫名とパラメータ", "書庫名:", "書庫形式:", "圧縮方式:", "パスワード (AES-256):",
    "ボリューム分割サイズ (例: 10m):", "ソリッド書庫を作成", "リカバリレコードを追加 (5%)", "ファイル名も暗号化", "書庫コメント:",
    "展開先とオプション", "展開先フォルダ:", "上書きモード:", "確認なしで上書き", "既存ファイルをスキップ", "自動的に名前を変更",
    "オプション:", "フルパスを保持", "完了後にフォルダを開く", "パスワード (暗号化時):",
    "ウィザード: 操作を選択 - OpenRAR", "何をしますか?", "書庫を展開する", "新しい書庫を作成する", "既存の書庫にファイルを追加する",
    "パスワードを入力してください:", "選択した項目を削除してもよろしいですか?",
    "Windows エクスプローラーの右クリックメニューを登録しました!", "右クリックメニューを削除しました。"
};

constexpr Table kRu = {
    "&Файл", "&Команды", "&Инструменты", "&Избранное", "&Параметры", "&Язык", "&Справка",
    "&Открыть архив...", "Открыть &папку...", "Задать &пароль по умолчанию...", "В&ыход",
    "&Добавить в архив...", "&Извлечь в указанную папку", "&Тестировать архив",
    "&Просмотр файла", "&Удалить файлы", "&Информация об архиве", "&Комментарий архива...",
    "&Мастер...", "&Найти файлы...", "&Восстановить архив", "Создать &SFX (.exe)", "&Тест производительности",
    "&Добавить в Избранное", "&Интегрировать в контекстное меню Windows", "&Удалить из контекстного меню Windows", "&О программе OpenRAR...",
    "Добавить", "Извлечь", "Тест", "Просмотр", "Удалить", "Поиск", "Мастер", "Инфо", "Исправить",
    "Имя", "Размер", "Тип", "Изменён", "Сжатый", "CRC32", "Системная папка", "Папка", "Файл",
    "Извлечь &сюда", "&Извлечь файлы...", "&Обновить",
    "Всего", "Выбрано", "байт в", "файле", "файлах", "Пароль задан",
    "ОК", "Отмена", "Обзор...", "Далее >",
    "Имя и параметры архива", "Имя архива:", "Формат архива:", "Метод сжатия:", "Пароль (AES-256):",
    "Разделить на тома (напр. 10m):", "Создать непрерывный архив", "Добавить данные для восстановления (5%)", "Шифровать имена файлов", "Комментарий:",
    "Путь и параметры извлечения", "Папка назначения:", "Режим перезаписи:", "Перезаписывать без запроса", "Пропускать существующие", "Переименовывать автоматически",
    "Параметры:", "Сохранять полные пути", "Открыть папку после завершения", "Пароль (если зашифрован):",
    "Мастер: Выбор операции - OpenRAR", "Что вы хотите сделать?", "Распаковать архив", "Создать новый архив", "Добавить файлы в существующий архив",
    "Введите пароль:", "Вы уверены, что хотите удалить выбранные элементы?",
    "Контекстное меню OpenRAR добавлено в Проводник Windows!", "Контекстное меню OpenRAR удалено."
};

constexpr Table kSr = {
    "&Datoteka", "&Komande", "&Alati", "&Omiljeno", "&Opcije", "&Jezik", "&Pomoć",
    "&Otvori arhivu...", "Izaberi &fasciklu...", "Podrazumevana &lozinka...", "&Izlaz",
    "&Dodaj u arhivu...", "&Raspakuj u izabranu fasciklu", "&Testiraj arhivu",
    "&Prikaži datoteku", "&Obriši datoteke", "Prikaži &informacije", "&Komentar arhive...",
    "&Čarobnjak...", "&Pronađi datoteke...", "&Popravi arhivu", "Napravi &SFX (.exe)", "&Test brzine",
    "&Dodaj u Omiljeno", "&Integriši u Windows desni klik meni", "&Ukloni iz Windows desni klik menija", "&O programu OpenRAR...",
    "Dodaj", "Raspakuj", "Testiraj", "Prikaži", "Obriši", "Pronađi", "Čarobnjak", "Info", "Popravi",
    "Naziv", "Veličina", "Tip", "Izmenjeno", "Spakovano", "CRC32", "Sistemska fascikla", "Fascikla", "Datoteka",
    "Raspakuj &ovde", "&Raspakuj datoteke...", "&Osveži",
    "Ukupno", "Izabrano", "bajtova u", "datoteci", "datoteka", "Lozinka postavljena",
    "U redu", "Otkaži", "Izaberi...", "Dalje >",
    "Naziv arhive i parametri", "Naziv arhive:", "Format arhive:", "Metod kompresije:", "Lozinka (AES-256):",
    "Podeli na delove (npr. 10m):", "Napravi solidnu arhivu", "Dodaj zapis za oporavak (5%)", "Šifruj nazive datoteka", "Komentar arhive:",
    "Putanja za raspakivanje i opcije", "Odredišna fascikla:", "Režim prepisivanja:", "Prepiši bez pitanja", "Preskoči postojeće", "Automatski preimenuj",
    "Opcije:", "Sačuvaj pune putanje", "Otvori fasciklu nakon završetka", "Lozinka (ako je šifrovano):",
    "Čarobnjak: Izaberite operaciju - OpenRAR", "Šta želite da uradite?", "Raspakuj arhivu", "Napravi novu arhivu", "Dodaj datoteke u postojeću arhivu",
    "Unesite lozinku:", "Da li ste sigurni da želite da obrišete izabrane stavke?",
    "OpenRAR meni za desni klik je dodat u Windows Explorer!", "OpenRAR meni za desni klik je uklonjen."
};

constexpr Table kHr = {
    "&Datoteka", "&Naredbe", "&Alati", "&Favoriti", "&Opcije", "&Jezik", "&Pomoć",
    "&Otvori arhivu...", "Odaberi &mapu...", "Zadana &lozinka...", "&Izlaz",
    "&Dodaj u arhivu...", "&Izdvoji u odabranu mapu", "&Testiraj arhivu",
    "&Pregledaj datoteku", "&Izbriši datoteke", "Prikaži &informacije", "&Komentar arhive...",
    "&Čarobnjak...", "&Pronađi datoteke...", "&Popravi arhivu", "Pretvori u &SFX (.exe)", "&Test brzine",
    "&Dodaj u Favorite", "&Integriraj u Windows izbornik desnog klika", "&Ukloni iz Windows izbornika desnog klika", "&O programu OpenRAR...",
    "Dodaj", "Izdvoji", "Testiraj", "Pregled", "Izbriši", "Pronađi", "Čarobnjak", "Info", "Popravi",
    "Naziv", "Veličina", "Vrsta", "Izmijenjeno", "Sažeto", "CRC32", "Sustavna mapa", "Mapa", "Datoteka",
    "Izdvoji &ovdje", "&Izdvoji datoteke...", "&Osvježi",
    "Ukupno", "Odabrano", "bajtova u", "datoteci", "datoteka", "Lozinka postavljena",
    "U redu", "Odustani", "Pregledaj...", "Dalje >",
    "Naziv arhive i parametri", "Naziv arhive:", "Format arhive:", "Metoda sažimanja:", "Lozinka (AES-256):",
    "Podijeli u dijelove (npr. 10m):", "Stvori čvrstu arhivu", "Dodaj zapis za oporavak (5%)", "Kriptiraj nazive datoteka", "Komentar arhive:",
    "Putanja izdvajanja i opcije", "Odredišna mapa:", "Način zamjene:", "Zamijeni bez upita", "Preskoči postojeće", "Automatski preimenuj",
    "Opcije:", "Sačuvaj pune putanje", "Otvori mapu nakon dovršetka", "Lozinka (ako je kriptirano):",
    "Čarobnjak: Odaberite radnju - OpenRAR", "Što želite učiniti?", "Raspakiraj arhivu", "Stvori novu arhivu", "Dodaj datoteke u postojeću arhivu",
    "Unesite lozinku:", "Jeste li sigurni da želite izbrisati odabrane stavke?",
    "OpenRAR izbornik desnog klika registriran je u Windows Exploreru!", "OpenRAR izbornik desnog klika je uklonjen."
};

constexpr Table kBs = {
    "&Datoteka", "&Naredbe", "&Alati", "&Favoriti", "&Opcije", "&Jezik", "&Pomoć",
    "&Otvori arhivu...", "Odaberi &direktorij...", "Zadana &šifra...", "&Izlaz",
    "&Dodaj u arhivu...", "&Raspakuj u odabrani direktorij", "&Testiraj arhivu",
    "&Pregledaj datoteku", "&Obriši datoteke", "Prikaži &informacije", "&Komentar arhive...",
    "&Čarobnjak...", "&Pronađi datoteke...", "&Popravi arhivu", "Napravi &SFX (.exe)", "&Test brzine",
    "&Dodaj u Favorite", "&Integriši u Windows meni desnog klika", "&Ukloni iz Windows menija desnog klika", "&O programu OpenRAR...",
    "Dodaj", "Raspakuj", "Testiraj", "Pregled", "Obriši", "Pronađi", "Čarobnjak", "Info", "Popravi",
    "Naziv", "Veličina", "Tip", "Izmijenjeno", "Kompresovano", "CRC32", "Sistemski direktorij", "Direktorij", "Datoteka",
    "Raspakuj &ovdje", "&Raspakuj datoteke...", "&Osvježi",
    "Ukupno", "Odabrano", "bajtova u", "datoteci", "datoteka", "Šifra postavljena",
    "U redu", "Otkaži", "Odaberi...", "Dalje >",
    "Naziv arhive i parametri", "Naziv arhive:", "Format arhive:", "Metod kompresije:", "Šifra (AES-256):",
    "Podijeli na dijelove (npr. 10m):", "Napravi solidnu arhivu", "Dodaj zapis za oporavak (5%)", "Šifruj nazive datoteka", "Komentar arhive:",
    "Putanja raspakivanja i opcije", "Odredišni direktorij:", "Način zamjene:", "Zamijeni bez pitanja", "Preskoči postojeće", "Automatski preimenuj",
    "Opcije:", "Sačuvaj pune putanje", "Otvori direktorij nakon završetka", "Šifra (ako je šifrovano):",
    "Čarobnjak: Odaberite operaciju - OpenRAR", "Šta želite uraditi?", "Raspakuj arhivu", "Napravi novu arhivu", "Dodaj datoteke u postojeću arhivu",
    "Unesite šifru:", "Da li ste sigurni da želite obrisati odabrane stavke?",
    "OpenRAR meni desnog klika je registrovan u Windows Exploreru!", "OpenRAR meni desnog klika je uklonjen."
};

constexpr std::array<const Table*, kLangCount> kTables = {
    &kEn, &kZh, &kEs, &kHi, &kAr, &kPt, &kId, &kFr, &kJa, &kRu, &kSr, &kHr, &kBs
};

} // namespace

const std::vector<LanguageInfo>& available_languages() {
    static const std::vector<LanguageInfo> langs = {
        {Language::En, "en", "English"},
        {Language::Zh, "zh", "中文 (Chinese)"},
        {Language::Es, "es", "Español (Spanish)"},
        {Language::Hi, "hi", "हिन्दी (Hindi)"},
        {Language::Ar, "ar", "العربية (Arabic)"},
        {Language::Pt, "pt", "Português (Portuguese)"},
        {Language::Id, "id", "Bahasa Indonesia"},
        {Language::Fr, "fr", "Français (French)"},
        {Language::Ja, "ja", "日本語 (Japanese)"},
        {Language::Ru, "ru", "Русский (Russian)"},
        {Language::Sr, "sr", "Srpski (Serbian)"},
        {Language::Hr, "hr", "Hrvatski (Croatian)"},
        {Language::Bs, "bs", "Bosanski (Bosnian)"}
    };
    return langs;
}

void set_language(Language lang) {
    int v = static_cast<int>(lang);
    if (v >= 0 && v < static_cast<int>(Language::Count)) {
        g_lang.store(v, std::memory_order_relaxed);
    }
}

bool set_language_code(const std::string& code) {
    std::string c = code;
    for (char& ch : c) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (c.size() > 2) c = c.substr(0, 2);
    for (const auto& info : available_languages()) {
        if (c == info.code) {
            set_language(info.id);
            return true;
        }
    }
    return false;
}

Language current_language() {
    return static_cast<Language>(g_lang.load(std::memory_order_relaxed));
}

const char* language_code(Language lang) {
    int idx = static_cast<int>(lang);
    const auto& langs = available_languages();
    if (idx >= 0 && static_cast<size_t>(idx) < langs.size()) return langs[static_cast<size_t>(idx)].code;
    return "en";
}

Language detect_system_language() {
#ifdef _WIN32
    LANGID id = GetUserDefaultUILanguage();
    WORD primary = PRIMARYLANGID(id);
    WORD sub = SUBLANGID(id);
    switch (primary) {
        case LANG_CHINESE:    return Language::Zh;
        case LANG_SPANISH:    return Language::Es;
        case LANG_HINDI:      return Language::Hi;
        case LANG_ARABIC:     return Language::Ar;
        case LANG_PORTUGUESE: return Language::Pt;
        case LANG_INDONESIAN: return Language::Id;
        case LANG_FRENCH:     return Language::Fr;
        case LANG_JAPANESE:   return Language::Ja;
        case LANG_RUSSIAN:    return Language::Ru;
        case LANG_SERBIAN:
            if (sub == SUBLANG_CROATIAN_CROATIA || sub == SUBLANG_CROATIAN_BOSNIA_HERZEGOVINA_LATIN)
                return Language::Hr;
            if (sub == SUBLANG_BOSNIAN_BOSNIA_HERZEGOVINA_LATIN || sub == SUBLANG_BOSNIAN_BOSNIA_HERZEGOVINA_CYRILLIC)
                return Language::Bs;
            return Language::Sr;
        default:              return Language::En;
    }
#else
    const char* env = std::getenv("OPENRAR_LANG");
    if (!env || !*env) env = std::getenv("LC_ALL");
    if (!env || !*env) env = std::getenv("LANG");
    if (env && *env) {
        std::string s(env);
        for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (s.size() >= 2) {
            std::string p2 = s.substr(0, 2);
            for (const auto& info : available_languages()) {
                if (p2 == info.code) return info.id;
            }
        }
    }
    return Language::En;
#endif
}

const char* tr(StrKey key) {
    size_t li = static_cast<size_t>(g_lang.load(std::memory_order_relaxed));
    size_t ki = static_cast<size_t>(key);
    if (li >= kLangCount) li = 0;
    if (ki >= kKeyCount) return "";
    const char* s = (*kTables[li])[ki];
    return (s && *s) ? s : kEn[ki];
}

} // namespace openrar::i18n
