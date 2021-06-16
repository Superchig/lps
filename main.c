#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/stat.h>

#include <alpm.h>
#include <termbox.h>

#define MAX_PACKAGE_NAME_SIZE 200

// alpm.h specific functions/structs

typedef struct _pkg_state_t
{
    alpm_pkg_t *underlying_pkg;
    bool is_selected;
} pkg_state_t;

typedef struct _pkg_state_list_t
{
    pkg_state_t *ary;
    int size;
    int capacity;
} pkg_state_list_t;

void pkg_state_list_add_pkg(pkg_state_list_t *list, alpm_pkg_t *underlying_pkg)
{
    if (list->size >= list->capacity)
    {
        list->capacity *= 2;
        list->ary = realloc(list->ary, sizeof(pkg_state_t) * list->capacity);
    }
    pkg_state_t *new_item = &list->ary[list->size];
    new_item->underlying_pkg = underlying_pkg;
    new_item->is_selected = false;
    list->size++;
}

pkg_state_list_t *pkg_state_list_new(int capacity)
{
    pkg_state_list_t *list = malloc(sizeof(pkg_state_list_t));
    list->ary = malloc(sizeof(pkg_state_t) * capacity);
    list->size = 0;
    list->capacity = capacity;
    return list;
}

void pkg_state_list_free(pkg_state_list_t *list)
{
    // This deallocates any allocated pkg_state_t *s
    free(list->ary);

    free(list);
}

// Compare the size of two alpm_pkg_t *s based off of their
// installed sizes.
int compare_pkgs(const void *_pkg_1, const void *_pkg_2)
{
    alpm_pkg_t *pkg_1 = (alpm_pkg_t *)_pkg_1;
    alpm_pkg_t *pkg_2 = (alpm_pkg_t *)_pkg_2;

    const int isize_1 = alpm_pkg_get_isize(pkg_1);
    const int isize_2 = alpm_pkg_get_isize(pkg_2);

    return isize_2 - isize_1;
}

int compare_pkg_states(const void *_pkg_state_1, const void *_pkg_state_2)
{
    pkg_state_t *pkg_state_1 = (pkg_state_t *)_pkg_state_1;
    pkg_state_t *pkg_state_2 = (pkg_state_t *)_pkg_state_2;

    const int isize_1 = alpm_pkg_get_isize(pkg_state_1->underlying_pkg);
    const int isize_2 = alpm_pkg_get_isize(pkg_state_2->underlying_pkg);

    return isize_2 - isize_1;
}

// termbox.h specific functions

void write_str(int x, int y, const char *line, uint32_t fg, uint32_t bg)
{
    while (*line != '\0')
    {
        tb_change_cell(x, y, *line, fg, bg);
        line++;
        x++;
    }
}

// miscellaneous functions

int min(int a, int b)
{
    return a < b ? a : b;
}

// FIXME(Chris): Remove if unnecessary
typedef struct _pkg_name
{
    char name[MAX_PACKAGE_NAME_SIZE];
    int size; // Size including NUL char
} pkg_name_t;

typedef struct _pkg_name_list
{
    pkg_name_t *names;
    int size; // The number of names currently in the list
    int capacity; // The maximum number of names the list has memory allocated for
} pkg_name_list_t;

pkg_name_list_t *pkg_name_list_new(int capacity)
{
    pkg_name_list_t *list = malloc(sizeof(pkg_name_list_t));
    list->names = malloc(sizeof(pkg_name_t) * capacity);
    list->size = 0;
    list->capacity = capacity;
    return list;
}

// TODO(Chris): Finish implementing/modify as necessary
pkg_name_t *pkg_name_new(pkg_name_list_t *list)
{
    if (list->size >= list->capacity)
    {
        list->capacity *= 2;
        list->names = realloc(list->names, sizeof(pkg_name_t) * list->capacity);
    }

    pkg_name_t *new_name = &list->names[list->size];
    new_name->name[0] = '\0';
    // Size is 1 because of the NUL character. strlen(name) would be 0.
    new_name->size = 1;
    list->size++;

    return new_name;
}

void pkg_name_list_free(pkg_name_list_t *list)
{
    free(list->names);

    free(list);
}

// Returns the number of characters read from *str_ref into buf
int read_word(const char **str_ref, char *buf, int capacity)
{
    int idx = 0;
    while (true)
    {
        buf[idx] = **str_ref;
        idx++;

        if (**str_ref == ' ')
        {
            *str_ref = &(*str_ref)[1];
            break;
        }

        if (**str_ref == '\0')
        {
            break;
        }

        // Update the single pointer of str_ref to be one item ahead.
        *str_ref = &(*str_ref)[1];

        if (idx >= capacity)
        {
            buf[capacity - 1] = '\0';
            break;
        }
    }

    buf[idx - 1] = '\0';

    return idx;
}

// Hash table implementation, which stores a pkg_name_t *
// Much thanks to https://benhoyt.com/writings/hash-table-in-c/

typedef struct _name_set_item
{
    unsigned long hash_value;
    pkg_name_t pkg_name; // Our key, though we store no value
    bool is_taken; // Will be set to 0 by default in calloc
} name_set_item_t;

typedef struct _name_set
{
    name_set_item_t *items;
    size_t size;
    size_t capacity; // Should always be powers of 2
} name_set_t;

// Dan Bernstein's djb2
// From http://www.cse.yorku.ca/~oz/hash.html
unsigned long hash(const char *str)
{
    unsigned long hash = 5381;
    int c;

    while ((c = *str++))
    {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }

    return hash;
}

name_set_t *name_set_new()
{
    name_set_t *set = malloc(sizeof(name_set_t));
    set->capacity = 4;
    set->size = 0;
    set->items = calloc(set->capacity, sizeof(name_set_item_t));
    return set;
}

void linear_probe_insert(name_set_item_t *items, size_t capacity, const pkg_name_t *pkg_name, unsigned long hash_value)
{
    size_t index = (size_t)(hash_value & (unsigned long)(capacity - 1));

    // Do a linear probe for insertion
    for (; index < capacity; index++)
    {
        if (!items[index].is_taken)
        {
            items[index].pkg_name = *pkg_name;
            items[index].hash_value = hash_value;
            items[index].is_taken = true;
            break;
        }
    }
}

void name_set_add_cpy(name_set_t *name_set, const pkg_name_t *pkg_name)
{
    name_set->size++;

    if (name_set->size >= name_set->capacity / 2)
    {
        const size_t orig_capacity = name_set->capacity;
        name_set->capacity *= 2;
        // name_set->items = realloc(name_set->items, sizeof(name_set_item_t) * name_set->capacity);
        name_set_item_t *new_items = calloc(name_set->capacity, sizeof(name_set_item_t));
        for (int i = 0; i < orig_capacity; i++)
        {
            if (name_set->items[i].is_taken)
            {
                linear_probe_insert(new_items, name_set->capacity, &name_set->items[i].pkg_name, name_set->items[i].hash_value);
            }
        }
        free(name_set->items);
        name_set->items = new_items;
    }

    unsigned long hash_value = hash(pkg_name->name);
    linear_probe_insert(name_set->items, name_set->capacity, pkg_name, hash_value);
}

void name_set_add_cpy_cstr(name_set_t *name_set, const char *str)
{
    pkg_name_t pkg_name;
    snprintf(pkg_name.name, MAX_PACKAGE_NAME_SIZE, "%s", str);
    pkg_name.size = strlen(str) + 1;

    name_set_add_cpy(name_set, &pkg_name);
}

// TODO(Chris): Move this to be with other pkg_name functions
bool pkg_name_eql(const pkg_name_t *pkg_name_1, const pkg_name_t *pkg_name_2)
{
    if (pkg_name_1->size != pkg_name_2->size)
    {
        return false;
    }

    for (int i = 0; i < pkg_name_1->size && i < pkg_name_2->size; i++)
    {
        if (pkg_name_1->name[i] != pkg_name_2->name[i])
        {
            return false;
        }
    }

    return true;
}

bool name_set_has(name_set_t *name_set, const pkg_name_t *pkg_name)
{
    unsigned long hash_value = hash(pkg_name->name);
    size_t index = (size_t)(hash_value & (unsigned long)(name_set->capacity - 1));

    while (name_set->items[index].is_taken)
    {
        if (name_set->items[index].hash_value == hash_value && pkg_name_eql(&name_set->items[index].pkg_name, pkg_name))
        {
            return true;
        }

        index++;

        if (index >= name_set->capacity)
        {
            index = 0;
        }
    }

    return false;
}

bool name_set_has_cstr(name_set_t *name_set, char *str)
{
    pkg_name_t pkg_name;
    snprintf(pkg_name.name, MAX_PACKAGE_NAME_SIZE, "%s", str);
    pkg_name.size = strlen(str) + 1;

    return name_set_has(name_set, &pkg_name);
}

void name_set_add_dependencies(name_set_t *name_set, alpm_db_t *localdb, char *name)
{
    // printf("%s\n", name);

    alpm_pkg_t *pkg = alpm_db_get_pkg(localdb, name);
    if (pkg != NULL && !name_set_has_cstr(name_set, name))
    {
        name_set_add_cpy_cstr(name_set, name);

        alpm_list_t *deps = alpm_pkg_get_depends(pkg);
        if (deps != NULL) // NULL deps means that there are no dependencies
        {
            // printf("deps size: %lu\n", alpm_list_count(deps));
            for (alpm_list_t *curr = deps; curr != NULL; curr = curr->next)
            {
                alpm_depend_t *dependency = (alpm_depend_t *)curr->data;
                // printf("\t%s\n", dependency->name);
                name_set_add_dependencies(name_set, localdb, dependency->name);
            }
        }
    }
}


void name_set_free(name_set_t *set)
{
    free(set->items);
    free(set);
}

int main()
{
    int err_return = 0;
    int tb_err = 0;

    FILE *keep_file = NULL;
    pkg_name_list_t *keep_package_names = NULL;
    pkg_name_list_t *unfound_package_names = NULL;
    name_set_t *dependencies_set = NULL;

    pkg_state_list_t *upgrade_list = NULL;
    alpm_errno_t alpm_errno = 0;

    alpm_handle_t *handle = alpm_initialize("/", "/var/lib/pacman", &alpm_errno);

    if (alpm_errno != 0)
    {
        printf("errno: %d\n", alpm_errno);

        // Apparently we don't need to free the result of alpm_strerror
        const char *strerror = alpm_strerror(alpm_errno);
        printf("strerror: %s\n", strerror);
        err_return = 10;
        goto exit;
    }

    // TODO(Chris): Parse in /etc/pacman.conf to dynamically find out which syncdbs are enabled
    alpm_db_t *localdb = alpm_get_localdb(handle);
    alpm_db_t *core = alpm_register_syncdb(handle, "core", 0);
    alpm_db_t *extra = alpm_register_syncdb(handle, "extra", 0);
    alpm_db_t *community = alpm_register_syncdb(handle, "community", 0);
    alpm_db_t *multilib = alpm_register_syncdb(handle, "multilib", 0);
    // Will contain all of the previously registered syncdbs
    alpm_list_t *dbs_sync = alpm_get_syncdbs(handle);
    alpm_list_t *packages = alpm_db_get_pkgcache(localdb);

    if (core == NULL)
    {
        printf("core syncdb failed to register.\n");
        err_return = 1;
        goto exit;
    }

    if (extra == NULL)
    {
        printf("extra syncdb failed to register.\n");
        err_return = 1;
        goto exit;
    }

    if (community == NULL)
    {
        printf("community syncdb failed to register.\n");
        err_return = 1;
        goto exit;
    }

    if (multilib == NULL)
    {
        printf("multilib syncdb failed to register.\n");
        err_return = 1;
        goto exit;
    }

    const char *home_path = getenv("HOME");
    char config_path[200];
    char config_dir_path[200];
    snprintf(config_path, 200, "%s/.config/lps/keep_packages", home_path);
    snprintf(config_dir_path, 200, "%s/.config/lps", home_path);

    struct stat s;
    int stat_err = stat(config_dir_path, &s);
    if (stat_err == -1)
    {
        if (errno == ENOENT)
        {
            if (mkdir(config_dir_path, 0755) == -1)
            {
                perror("Failed to create config directory");
                err_return = 30;
                goto exit;
            }
        }
        else
        {
            perror("stat");
            err_return = 31;
            goto exit;
        }
    }
    else if (!S_ISDIR(s.st_mode))
    {
        fprintf(stderr, "Config dir is not a directory");
        err_return = 32;
        goto exit;
    }

    keep_file = fopen(config_path, "a+");
    if (keep_file == NULL)
    {
        perror("Failed to open config file");
        err_return = 35;
        goto exit;
    }

    keep_package_names = pkg_name_list_new(5);
    while (true)
    {
        pkg_name_t *new_name = pkg_name_new(keep_package_names);
        char *result = fgets(new_name->name, MAX_PACKAGE_NAME_SIZE, keep_file);
        if (result == NULL)
        {
            keep_package_names->size--; // Remove the last allocated item
            break;
        }

        new_name->size = strlen(new_name->name) + 1;

        if (new_name->name[new_name->size - 2] == '\n')
        {
            new_name->name[new_name->size - 2] = '\0';
            new_name->size--;
        }

        // printf("%s\n", new_name->name);
    }

    // Add default keep packages if none were read in from file
    if (keep_package_names->size <= 0)
    {
        snprintf(pkg_name_new(keep_package_names)->name, MAX_PACKAGE_NAME_SIZE, "pacman");
        snprintf(pkg_name_new(keep_package_names)->name, MAX_PACKAGE_NAME_SIZE, "glibc");
    }

    dependencies_set = name_set_new();
    // name_set_add_dependencies(dependencies_set, localdb, "gnome-desktop");

    // alpm_pkg_t *systemd = alpm_db_get_pkg(localdb, "systemd");
    // alpm_list_t *system_deps = alpm_pkg_get_depends(systemd);
    // printf("systemd deps: ");
    // for (alpm_list_t *curr = system_deps; curr != NULL; curr = curr->next)
    // {
    //     alpm_depend_t *dependency = (alpm_depend_t *)curr->data;
    //     printf("%s ", dependency->name);
    // }
    // printf("\n");

    unfound_package_names = pkg_name_list_new(5); // TODO(Chris): Do something with the unfound packages?
    for (int i = 0; i < keep_package_names->size; i++)
    {
        pkg_name_t *pkg_name = &keep_package_names->names[i];
        alpm_pkg_t *pkg = alpm_db_get_pkg(localdb, keep_package_names->names[i].name);

        if (pkg == NULL)
        {
            pkg_name_t *new_name = pkg_name_new(unfound_package_names);
            *new_name = *pkg_name;
        }
        else
        {
            // printf("keep_package_names: %s\n", keep_package_names->names[i].name);
            name_set_add_dependencies(dependencies_set, localdb, pkg_name->name);
            // name_set_add_cpy(dependencies_set, pkg_name);
        }
    }

    // printf("size: %lu\n", dependencies_set->size);
    // for (int i = 0; i < dependencies_set->capacity; i++)
    // {
    //     if (dependencies_set->items[i].is_taken)
    //     {
    //         printf(" true: %s\n", dependencies_set->items[i].pkg_name.name);
    //     }
    //     else
    //     {
    //         printf("false:\n");
    //     }
    // }

    // pkg_name_t test_name = {"qtcreator", 10};
    // printf("qtcreator: %s\n", name_set_has(dependencies_set, &test_name) ? "true" : "false");

    // pkg_name_t test_name = {"systemd", 8};
    // printf("%s: %s\n", test_name.name, name_set_has(dependencies_set, &test_name) ? "true" : "false");

    /// Initialize packages to upgrade

    upgrade_list = pkg_state_list_new(5);
    for (alpm_list_t *curr = packages; curr != NULL; curr = curr->next)
    {
        alpm_pkg_t *package = (alpm_pkg_t *)curr->data;
        alpm_pkg_t *new_version = alpm_sync_get_new_version(package, dbs_sync);
        if (new_version != NULL)
        {
            const char *raw_name = alpm_pkg_get_name(package);
            pkg_name_t pkg_name;
            snprintf(pkg_name.name, MAX_PACKAGE_NAME_SIZE, "%s", raw_name);
            pkg_name.size = strlen(raw_name) + 1;

            if (!name_set_has(dependencies_set, &pkg_name))
            {
                pkg_state_list_add_pkg(upgrade_list, new_version);
                // printf("%s\n", alpm_pkg_get_name(new_version));
            }
        }
    }

    name_set_free(dependencies_set);
    pkg_name_list_free(unfound_package_names);

    qsort(upgrade_list->ary, upgrade_list->size, sizeof(pkg_state_t), compare_pkg_states);

    if (upgrade_list->size <= 0)
    {
        err_return = 20;
        fprintf(stderr, "There are no currently packages to upgrade. Try `sudo pacman -Sy` or removing packages from the keep list.");
        goto exit;
    }

    tb_err = tb_init();

    if (tb_err < 0)
    {
        printf("Failed to initialize termbox\n");
        err_return = 100;
        goto exit;
    }

    /// Main input loop

    int selection_index = 0;
    int base_index = 0;
    while (true)
    {
        // Recalculate selection_index in case of window resizing
        const int bottom_line = tb_height() - 1;
        if (selection_index >= bottom_line)
        {
            selection_index = bottom_line;
        }

        int poll_err = 0;
        const int pkg_index = base_index + selection_index;
        pkg_state_t *curr_pkg = &upgrade_list->ary[pkg_index];
        const int half_width = tb_width() / 2;
        int view_height = min(tb_height(), upgrade_list->size);
        for (int i = 0; i < view_height; i++)
        {
            const char *pkg_name = alpm_pkg_get_name(upgrade_list->ary[base_index + i].underlying_pkg);
            const int len = strlen(pkg_name);

            uint32_t fg = TB_DEFAULT;

            if (upgrade_list->ary[base_index + i].is_selected)
            {
                // If the background is bold, then the text blinks.
                // So we only make the foreground bold.
                fg = TB_YELLOW;
                fg |= TB_BOLD;
            }

            if (i == selection_index)
            {
                fg |= TB_REVERSE;
            }

            write_str(0, i, pkg_name, fg, TB_DEFAULT);
            for (int col = len; col < half_width; col++)
            {
                tb_change_cell(col, i, ' ', fg, TB_DEFAULT);
            }
        }

        const char *desc = alpm_pkg_get_desc(curr_pkg->underlying_pkg);
        int curs_x = tb_width() / 2;
        int curs_y = 0;
        while (*desc != '\0')
        {
            char buf[80]; // Let's hope no words are longer than 80 characters

            int chars_read = read_word(&desc, buf, 80);
            if (curs_x + chars_read > tb_width())
            {
                curs_x = tb_width() / 2;
                curs_y++;
            }
            write_str(curs_x, curs_y, buf, TB_DEFAULT, TB_DEFAULT);

            curs_x += chars_read;
        }

        tb_present();

        struct tb_event event;
        poll_err = tb_poll_event(&event);

        if (poll_err == -1)
        {
            err_return = 15;
            goto exit_tb;
        }

        if (event.type == TB_EVENT_KEY)
        {
            if (event.ch == 0)
            {
                switch (event.key)
                {
                case TB_KEY_SPACE:
                case TB_KEY_ENTER:
                    if (true) // This is just here to get QTCreator's formatting to work correctly
                    {
                        curr_pkg->is_selected = !curr_pkg->is_selected;

                        // NOTE(Chris): This currently just copy-pastes the functionality of the 'j' key.
                        // We might want to make this a little more DRY in the future, but that might
                        // require making the whole input handling system more robust.
                        if (selection_index == view_height - 1)
                        {
                            base_index++;
                        }
                        else if (selection_index < view_height)
                        {
                            selection_index++;
                        }
                    }
                    break;
                case TB_KEY_CTRL_U:
                    base_index -= tb_height() / 2;

                    if (base_index < 0)
                    {
                        base_index = 0;

                        selection_index -= tb_height() / 2;
                        if (selection_index < 0)
                        {
                            selection_index = 0;
                        }
                    }

                    if (selection_index < 0)
                    {
                        selection_index = 0;
                    }
                    break;
                case TB_KEY_CTRL_D:
                    base_index += tb_height() / 2;

                    if (base_index >= upgrade_list->size - bottom_line)
                    {
                        base_index = upgrade_list->size - bottom_line - 1;

                        selection_index += tb_height() / 2;
                    }
                    break;
                }
            }
            else
            {
                switch (event.ch)
                {
                case 'q':
                    goto exit_tb;
                case 'j':
                    if (base_index + selection_index < upgrade_list->size - 1)
                    {
                        if (selection_index == view_height - 1)
                        {
                            base_index++;
                        }
                        else if (selection_index < view_height)
                        {
                            selection_index++;
                        }
                    }
                    break;
                case 'k':
                    if (base_index + selection_index > 0)
                    {
                        if (selection_index == 0)
                        {
                            base_index--;
                        }
                        else if (selection_index > 0)
                        {
                            selection_index--;
                        }
                    }
                    break;
                }
            }
        }

        tb_clear();
    }

exit_tb:
    tb_shutdown();

exit:
    if (keep_file != NULL)
    {
        rewind(keep_file);

        for (int i = 0; i < keep_package_names->size; i++)
        {
            fprintf(keep_file, "%s\n", keep_package_names->names[i].name);
        }

        fclose(keep_file);
    }

    pkg_name_list_free(keep_package_names);

    if (upgrade_list != NULL)
    {
        bool was_at_least_one_selected = false;
        for (int i = 0; i < upgrade_list->size; i++)
        {
            if (upgrade_list->ary[i].is_selected)
            {
                printf("%s ", alpm_pkg_get_name(upgrade_list->ary[i].underlying_pkg));
                was_at_least_one_selected = true;
            }
        }
        if (was_at_least_one_selected)
        {
            printf("\n");
        }

        pkg_state_list_free(upgrade_list);
    }

    alpm_release(handle);

    return err_return;
}
